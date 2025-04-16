/* SPDX-License-Identifier: BSD-2-Clause */
/* X-SPDX-Copyright-Text: (c) Copyright 2023 Advanced Micro Devices, Inc. */

#include "ef_vi_internal.h"
#include "shrub_client.h"

#ifndef __KERNEL__
#include <stddef.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>

static int client_socket(void)
{
  int rc = socket(AF_UNIX, SOCK_STREAM, 0);
  return rc >= 0 ? rc : -errno;
}

static int client_connect(int client, const char* server_addr)
{
  struct sockaddr_un addr;
  int path_len = strlen(server_addr);

  if( path_len >= sizeof(addr.sun_path) )
    return -EINVAL;

  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, server_addr);
  /* TBD: do we want a non-blocking option (for this and recv)? */
  return connect(client, (struct sockaddr*)&addr,
                 offsetof(struct sockaddr_un, sun_path) + path_len + 1);
}

static size_t buffer_mmap_bytes(struct ef_shrub_shared_metrics* metrics)
{
  return metrics->buffer_bytes * metrics->buffer_count;
}

static size_t server_mmap_bytes(struct ef_shrub_shared_metrics* metrics)
{
  return metrics->server_fifo_size * sizeof(ef_shrub_buffer_id);
}

static size_t client_fifo_bytes(struct ef_shrub_shared_metrics* metrics)
{
  return metrics->client_fifo_size * sizeof(ef_shrub_buffer_id);
}

static size_t client_mmap_bytes(struct ef_shrub_shared_metrics* metrics)
{
  return client_fifo_bytes(metrics) + sizeof(struct ef_shrub_client_state);
}

static int client_mmap(struct ef_shrub_client* client,
                       struct ef_shrub_shared_metrics* metrics,
                       void* buffers,
                       int shared_fds[EF_SHRUB_FD_COUNT])
{
  void* map;
  int flags = MAP_SHARED | MAP_POPULATE;

  map = mmap(buffers, buffer_mmap_bytes(metrics), PROT_READ,
             flags | MAP_HUGETLB | MAP_FIXED,
             shared_fds[EF_SHRUB_FD_BUFFERS], 0);
  if( map == MAP_FAILED )
    return -errno;
  client->buffers = map;

  map = mmap(NULL, server_mmap_bytes(metrics), PROT_READ, flags,
             shared_fds[EF_SHRUB_FD_SERVER_FIFO], 0);
  if( map == MAP_FAILED )
    return -errno;
  client->server_fifo = map;

  map = mmap(NULL, client_mmap_bytes(metrics), PROT_READ | PROT_WRITE, flags,
             shared_fds[EF_SHRUB_FD_CLIENT_FIFO],
             metrics->client_fifo_offset);
  if( map == MAP_FAILED )
    return -errno;
  client->client_fifo = map;
  client->state = (void*)((char*)map + client_fifo_bytes(metrics));

  return 0;
}

static int client_send_request(int socket, struct ef_shrub_request *request)
{
  int rc = send(socket, request, sizeof(*request), 0);

  if( rc < 0 )
    return -errno;
  if( rc < sizeof(*request) )
    return -EIO;

  return 0;
}


static int client_request_queue(struct ef_shrub_client *client,
                                const char* server_addr, int qid)
{
  int rc = 0;
  struct ef_shrub_request request;

  rc = client_connect(client->socket, server_addr);
  if( rc < 0 )
    return rc;

  request.server_version = EF_SHRUB_VERSION;
  request.type = EF_SHRUB_REQUEST_QUEUE;
  request.requests.queue.qid = qid;
  return client_send_request(client->socket, &request);
}

static void client_munmap(struct ef_shrub_client* client)
{
  struct ef_shrub_shared_metrics* metrics = &client->state->metrics;
  if( client->buffers )
    munmap(client->buffers, buffer_mmap_bytes(metrics));
  if( client->server_fifo )
    munmap(client->server_fifo, server_mmap_bytes(metrics));
  if( client->client_fifo )
    munmap(client->client_fifo, client_mmap_bytes(metrics));
}

static int client_recv_metrics(struct ef_shrub_client* client, void* buffers)
{
  int rc, i;
  struct ef_shrub_shared_metrics metrics;
  struct iovec iov = {
    .iov_base = &metrics,
    .iov_len = sizeof(metrics)
  };
  int shared_fds[EF_SHRUB_FD_COUNT];
  char cmsg_buf[CMSG_SPACE(sizeof(shared_fds))];
  struct msghdr msg = {
    .msg_iov = &iov,
    .msg_iovlen = 1,
    .msg_control = cmsg_buf,
    .msg_controllen = sizeof(cmsg_buf)
  };
  struct cmsghdr* cmsg;

  rc = recvmsg(client->socket, &msg, 0);
  if( rc < 0 )
    return -errno;
  if( rc != sizeof(metrics) || metrics.server_version != EF_SHRUB_VERSION )
    return -EPROTO;

  cmsg = CMSG_FIRSTHDR(&msg);
  if( cmsg == NULL ||
      cmsg->cmsg_level != SOL_SOCKET ||
      cmsg->cmsg_type != SCM_RIGHTS ||
      cmsg->cmsg_len != CMSG_LEN(sizeof(shared_fds)) )
    return -EPROTO;

  memcpy(shared_fds, CMSG_DATA(cmsg), sizeof(shared_fds));
  rc = client_mmap(client, &metrics, buffers, shared_fds);
  for( i = 0; i < EF_SHRUB_FD_COUNT; ++i )
    close(shared_fds[i]);
  if( rc < 0 )
    return rc;
  return 0;
}

static int client_request_token(int sock, const char *server_addr)
{
  struct ef_shrub_request request = {0};
  int rc;

  rc = client_connect(sock, server_addr);
  if( rc )
    return rc;

  request.server_version = EF_SHRUB_VERSION;
  request.type = EF_SHRUB_REQUEST_TOKEN;
  return client_send_request(sock, &request);
}

int ef_shrub_client_request_token(const char *server_addr,
                                  struct ef_shrub_token_response *response)
{
  int sock;
  int rc;

  rc = client_socket();
  if( rc < 0 )
    return rc;

  sock = rc;
  rc = client_request_token(sock, server_addr);
  if( rc < 0 )
    goto out;

  rc = recv(sock, response, sizeof(*response), 0);
  if( rc < 0 )
    rc = -errno;
  else if( rc < sizeof(*response) )
    rc = -EPROTO;
  else
    rc = 0;

out:
  close(sock);
  return rc;
}

int ef_shrub_client_open(struct ef_shrub_client* client,
                         void* buffers,
                         const char* server_addr,
                         int qid)
{
  int rc;
  memset(client, 0, sizeof(*client));

  rc = client_socket();
  if( rc < 0 )
    goto fail_socket;
  client->socket = rc;

  rc = client_request_queue(client, server_addr, qid);
  if( rc < 0 )
    goto fail_request_queue;

  rc = client_recv_metrics(client, buffers);
  if( rc < 0 )
    goto fail_recv;

  return 0;

fail_recv:
  client_munmap(client);
fail_request_queue:
  close(client->socket);
fail_socket:
  return rc;
}

void ef_shrub_client_close(struct ef_shrub_client* client)
{
  client_munmap(client);
  close(client->socket);
}
#endif

int ef_shrub_client_acquire_buffer(struct ef_shrub_client* client,
                                   uint32_t* buffer_id,
                                   bool* sentinel)
{

  ci_dword_t id2;
  int i = client->state->server_fifo_index;
  ef_shrub_buffer_id id = client->server_fifo[i];
  if( id == EF_SHRUB_INVALID_BUFFER )
    return -EAGAIN;

  client->state->server_fifo_index =
    i == client->state->metrics.server_fifo_size - 1 ? 0 : i + 1;

  id2.u32[0] = id;
  *buffer_id = CI_DWORD_FIELD(id2, EF_SHRUB_BUFFER_ID);
  *sentinel = CI_DWORD_FIELD(id2, EF_SHRUB_SENTINEL) == 1;
  return 0;
}

void ef_shrub_client_release_buffer(struct ef_shrub_client* client,
                                    uint32_t buffer_id)
{
  int i = client->state->client_fifo_index;

  client->client_fifo[i] = buffer_id;
  client->state->client_fifo_index =
    i == client->state->metrics.client_fifo_size - 1 ? 0 : i + 1;
}

bool ef_shrub_client_buffer_available(const struct ef_shrub_client* client)
{
  int i = client->state->server_fifo_index;
  ef_shrub_buffer_id id = client->server_fifo[i];
  return id != EF_SHRUB_INVALID_BUFFER;
}
