/* SPDX-License-Identifier: GPL-2.0 */
/* X-SPDX-Copyright-Text: (c) Copyright 2012-2020 Xilinx, Inc. */
#ifndef __ONLOAD_KERNEL_COMPAT_H__
#define __ONLOAD_KERNEL_COMPAT_H__

#include <driver/linux_resource/autocompat.h>
#include <linux/file.h>
#include <linux/signal.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/seq_file.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/fdtable.h>

#if defined(CONFIG_COMPAT) && defined(CONFIG_X86_64) && !defined(TIF_IA32)
/* linux>=5.11: user_64bit_mode() requires this */
#include <linux/sched/task_stack.h>
#endif


#ifndef __NFDBITS
# define __NFDBITS BITS_PER_LONG
#endif


#ifndef EFRM_HAVE_REINIT_COMPLETION
#define reinit_completion(c) INIT_COMPLETION(*c)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0))
#define ci_call_usermodehelper call_usermodehelper
#else
extern int
ci_call_usermodehelper(char *path, char **argv, char **envp, int wait);
#endif


static inline struct file *ci_get_file_rcu(struct file **f)
{
#ifdef EFRM_HAVE_GET_FILE_RCU_FUNC
  return get_file_rcu(f);
#else
  /* In linux < 6.7 get_file_rcu() was defined as a macro, like this:
   * #define get_file_rcu(x) atomic_long_inc_not_zero(&(x)->f_count)
   * Use the same implementation, but match new get_file_rcu() prototype. */
  struct file *file = *f;
  return atomic_long_inc_not_zero(&file->f_count) ? file : NULL;
#endif /* EFRM_HAVE_GET_FILE_RCU_FUNC */
}

/* init_timer() was removed in Linux 4.15, with timer_setup()
 * replacing it */
#ifndef EFRM_HAVE_TIMER_SETUP
#define timer_setup(timer, callback, flags)     \
  init_timer(timer);                            \
  (timer)->data = 0;                            \
  (timer)->function = &callback;
#endif


/* In linux-5.0 access_ok() lost its first parameter.
 * See bug 85932 comment 7 why we can't redefine access_ok().
 */
#ifndef EFRM_ACCESS_OK_HAS_2_ARGS
#define efab_access_ok(addr, size) access_ok(VERIFY_WRITE, addr, size)
#else
#define efab_access_ok access_ok
#endif

/* is_compat_task() was removed for x86 in linux-4.6 */
#if defined(EFRM_NEED_IS_COMPAT_TASK) && !defined(CONFIG_ARM64)
/* ARM64 kernels provide is_compat_task(), only define for other archs */
static inline int is_compat_task(void)
{
#if !defined(CONFIG_COMPAT)
  return 0;
#elif defined(CONFIG_X86_64)
  #ifdef TIF_IA32
    return test_thread_flag(TIF_IA32);
  #else
    return !user_64bit_mode(task_pt_regs(current));
  #endif
#elif defined(CONFIG_PPC64)
  return test_thread_flag(TIF_32BIT);
#else
  #error "cannot define is_compat_task() for this architecture"
#endif
}
#endif

/* skb_frag_off() was added in linux-5.4 */
#ifdef EFRM_NEED_SKB_FRAG_OFF
/**
 * skb_frag_off() - Returns the offset of a skb fragment
 * @frag: the paged fragment
 */
static inline unsigned int skb_frag_off(const skb_frag_t *frag)
{
	/* This later got renamed bv_offset (because skb_frag_t is now really
	 * a struct bio_vec), but the page_offset name should work in any
	 * kernel that doesn't already have skb_frag_off defined.
	 */
	return frag->page_offset;
}
#endif


#ifdef EFRM_HAVE_NETDEV_REGISTER_RH
/* The _rh versions of these appear in RHEL7.3.
 * Wrap them to make the calling code simpler.
 */
static inline int efrm_register_netdevice_notifier(struct notifier_block *b)
{
	return register_netdevice_notifier_rh(b);
}

static inline int efrm_unregister_netdevice_notifier(struct notifier_block *b)
{
	return unregister_netdevice_notifier_rh(b);
}

#define register_netdevice_notifier efrm_register_netdevice_notifier
#define unregister_netdevice_notifier efrm_unregister_netdevice_notifier
#endif

static inline struct fown_struct* efrm_file_f_owner(struct file *file)
{
#ifdef EFRM_F_OWNER_IS_VAL
	return &file->f_owner;
#else
	/* linux 6.12+ */
	return file->f_owner;
#endif
}


static inline int
oo_copy_file_owner(struct file *file_to, struct file *file_from)
{
#ifndef EFRM_F_OWNER_IS_VAL
  /* linux 6.12 */
  int rc;

  if( efrm_file_f_owner(file_from) == NULL )
    return 0;

  rc = file_f_owner_allocate(file_to);
  if( rc != 0 )
    return rc;
#endif

  if(efrm_file_f_owner(file_from)->pid != 0) {
    rcu_read_lock();
    __f_setown(file_to, efrm_file_f_owner(file_from)->pid,
               efrm_file_f_owner(file_from)->pid_type, 1);
    rcu_read_unlock();
  }
  efrm_file_f_owner(file_to)->signum = efrm_file_f_owner(file_from)->signum;

  return 0;
}

#if defined(EFRM_CLOEXEC_FILES_STRUCT) || LINUX_VERSION_CODE >= KERNEL_VERSION(6,11,0)
/* linux 6.11+ - close_on_exec takes files_struct directly */
#define efrm_close_on_exec close_on_exec
#else
static inline bool efrm_close_on_exec(unsigned int fd,
				      const struct files_struct *files)
{
	return close_on_exec(fd, files_fdtable(files));
}
#endif /* EFRM_CLOEXEC_FILES_STRUCT || kernel >= 6.11 */

#ifdef EFRM_HAVE_SKB_RECV_NOBLOCK_PARAM
static inline struct sk_buff *efrm_skb_recv_datagram(struct sock *sk,
                                                     unsigned flags,
                                                     int *err)
{
  return skb_recv_datagram(sk, flags, flags & MSG_DONTWAIT ? 1 : 0, err);
}
#else
/* linux 5.19+ */
#define efrm_skb_recv_datagram skb_recv_datagram
#endif

#ifdef EFRM_HAVE_TIMER_DELETE_SYNC
/* linux 6.1+ */
#define efrm_timer_delete_sync timer_delete_sync
#else
#define efrm_timer_delete_sync del_timer_sync
#endif

#endif /* __ONLOAD_KERNEL_COMPAT_H__ */
