#include "common.h"
#include "kernel.hh"
#include "proc.hh"

namespace syscall
{
    using sys::statcode;
    using namespace resource;
    sysrt_t getPid(){
        return kHartObj().curtask->getProcess()->pid();
    }
    sysrt_t getPPid() { return kHartObj().curtask->getProcess()->parentProc()->pid(); }
    sysrt_t getUid() {
        return kHartObj().curtask->getProcess()->ruid();
    }
    sysrt_t getEuid() {
        return kHartObj().curtask->getProcess()->euid();
    }
    sysrt_t getGid() {
        return kHartObj().curtask->getProcess()->rgid();
    }
    sysrt_t getEgid() {
        return kHartObj().curtask->getProcess()->egid();
    }
    sysrt_t getTid() {
        return kHartObj().curtask->tid();
    }
    
    sysrt_t setGid(gid_t a_gid) {
        auto curproc = kHartObj().curtask->getProcess();
        curproc->rgid() = curproc->egid() = curproc->sgid() = a_gid;
        return statcode::ok;
    }
    sysrt_t setUid(uid_t a_uid) {
        auto curproc = kHartObj().curtask->getProcess();
        curproc->ruid() = curproc->euid() = curproc->suid() = a_uid;
        return statcode::ok;
    }
    sysrt_t getRESgid(gid_t *a_rgid,gid_t *a_egid,gid_t *a_sgid) {
        if(a_rgid==nullptr || a_egid==nullptr || a_sgid==nullptr) { return -EFAULT; }
        
        auto curproc = kHartObj().curtask->getProcess();
        curproc->vmar.copyout((xlen_t)a_rgid, ByteArray((uint8*)&curproc->rgid(), sizeof(gid_t)));
        curproc->vmar.copyout((xlen_t)a_egid, ByteArray((uint8*)&curproc->egid(), sizeof(gid_t)));
        curproc->vmar.copyout((xlen_t)a_sgid, ByteArray((uint8*)&curproc->sgid(), sizeof(gid_t)));

        return statcode::ok;
    }
    sysrt_t getRESuid(uid_t *a_ruid,uid_t *a_euid,uid_t *a_suid) {
        if(a_ruid==nullptr || a_euid==nullptr || a_suid==nullptr) { return -EFAULT; }
        
        auto curproc = kHartObj().curtask->getProcess();
        curproc->vmar.copyout((xlen_t)a_ruid, ByteArray((uint8*)&curproc->ruid(), sizeof(uid_t)));
        curproc->vmar.copyout((xlen_t)a_euid, ByteArray((uint8*)&curproc->euid(), sizeof(uid_t)));
        curproc->vmar.copyout((xlen_t)a_suid, ByteArray((uint8*)&curproc->suid(), sizeof(uid_t)));

        return statcode::ok;
    }
    sysrt_t setPGid(pid_t a_pid,pid_t a_pgid) {
        auto proc = (a_pid==0 ? kHartObj().curtask->getProcess() : (**kGlobObjs->procMgr)[a_pid]);
        if(proc == nullptr) { return -ESRCH; }
        proc->pgid() = (a_pgid==0 ? proc->pid() : a_pgid);

        return statcode::ok;
    }
    sysrt_t getPGid(pid_t a_pid) {
        auto proc = (a_pid==0 ? kHartObj().curtask->getProcess() : (**kGlobObjs->procMgr)[a_pid]);
        if(proc == nullptr) { return -ESRCH; }

        return proc->pgid();
    }
    sysrt_t getSid(pid_t a_pid) {
        auto proc = (a_pid==0 ? kHartObj().curtask->getProcess() : (**kGlobObjs->procMgr)[a_pid]);
        if(proc == nullptr) { return -ESRCH; }

        return proc->sid();
    }
    sysrt_t setSid() {
        auto curproc = kHartObj().curtask->getProcess();
        if(curproc->pgid() == curproc->pid()) { return -EPERM; }
        curproc->sid()  = curproc->pid();
        curproc->pgid() = curproc->pid();

        return curproc->sid();
    }
    sysrt_t getGroups(int a_size,gid_t *a_list) {
        if(a_list == nullptr) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        if(a_size == 0) { return curproc->getGroupsNum(); }
        ByteArray grps = curproc->getGroups(a_size);
        curproc->vmar.copyout((xlen_t)a_list, grps);

        return grps.len / sizeof(gid_t);
    }
    sysrt_t setGroups(int a_size,gid_t *a_list) {
        if(a_list == nullptr) { return -EFAULT; }

        auto curproc = kHartObj().curtask->getProcess();
        ByteArray listarr = curproc->vmar.copyin((xlen_t)a_list, a_size * sizeof(gid_t));
        ArrayBuff<gid_t> grps((gid_t*)listarr.buff, a_size);
        curproc->setGroups(grps);

        return 0;
    }

    sysrt_t getRLimit(int a_rsrc,RLim *a_rlim) {
        if(a_rlim==nullptr || a_rsrc<0 || a_rsrc>=RSrc::RLIMIT_NLIMITS) { return -EFAULT; }
        
        auto curproc = kHartObj().curtask->getProcess();
        curproc->vmar.copyout((xlen_t)a_rlim, curproc->getRLimit(a_rsrc));

        return 0;
    }
    sysrt_t setRLimit(int a_rsrc,RLim *a_rlim) {
        if(a_rlim==nullptr || a_rsrc<0 || a_rsrc>=RSrc::RLIMIT_NLIMITS) { return -EFAULT; }
        
        auto curproc = kHartObj().curtask->getProcess();
        ByteArray rlimarr = curproc->vmar.copyin((xlen_t)a_rlim, sizeof(RLim));
        const RLim *rlim = (const RLim*)rlimarr.buff;
        if(rlim->rlim_cur > rlim->rlim_max) { return statcode::err; }
        
        return curproc->setRLimit(a_rsrc, rlim);
    }
    sysrt_t uMask(mode_t a_mask) {
        auto curproc = kHartObj().curtask->getProcess();
        return curproc->setUMask(a_mask);
    }
} // namespace syscall
