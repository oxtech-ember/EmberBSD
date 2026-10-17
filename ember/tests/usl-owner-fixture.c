/* SPDX-License-Identifier: BSD-2-Clause */
static struct credential ordinary_cred = { 0, 1001 }, admin_cred = { 1, 0 };
static struct credential other_cred = { 0, 1002 };
static struct proc foreign = { 43, &process_lock };
static struct proc fork_child = { 44, &process_lock };
static struct lwp owner_lwp = { &process, &ordinary_cred };
static struct lwp foreign_lwp = { &foreign, &ordinary_cred };
static struct lwp child_lwp = { &fork_child, &ordinary_cred };
static struct lwp admin_lwp = { &foreign, &admin_cred };
static int request(struct lwp *l, u_long cmd, void *data)
{ return wsdisplay_usl_ioctl2(NULL, &screen, cmd, data, 0, l); }
static void exit_owner(void) { doexithooks(&process); }
static void owner_check(const char *name)
{
    assert(!hook_count && LIST_EMPTY(&exithook_list));
    assert(!hook_readers && !exithook_lock.writer);
    check(name);
}
static void auth_destroy(void)
{
    usl_sync_destroy(sd);
    advance(1);
    assert(!frees); /* ioctl pin keeps its unpublished cookie alive. */
}
static void retirement_exit(void)
{
    doexithooks(&process);
    assert(!frees); /* writer acquisition drains the prior reader dispatch. */
}
static void owner_tests(void)
{
    struct vt_mode automatic = { .mode = VT_AUTO };
    struct vt_mode controlled = { .mode = VT_PROCESS, .relsig = 10, .acqsig = 12 };
    long ack;
    for (int attach = 0; attach < 2; attach++) {
        for (int allow = 0; allow < 2; allow++) {
            setup(); ack = attach ? VT_ACKACQ : (allow ? VT_TRUE : VT_FALSE);
            assert((attach ? usl_attachproc : usl_detachproc)(sd, 0, completed, &screen) == EAGAIN);
            int flags = sd->s_flags;
            u_int generation = sd->s_generation;
            assert(request(&foreign_lwp, VT_RELDISP, &ack) == EPERM);
            assert(request(&child_lwp, VT_RELDISP, &ack) == EPERM);
            foreign_lwp.l_cred = &other_cred;
            assert(request(&foreign_lwp, VT_RELDISP, &ack) == EPERM);
            foreign_lwp.l_cred = &ordinary_cred;
            assert(sd->s_flags == flags && sd->s_generation == generation);
            assert(!callbacks && !signals && screen.cookie == sd);
            assert(request(&owner_lwp, VT_RELDISP, &ack) == 0);
            assert(callbacks == 1 && result == (attach || allow ? 0 : EIO)); finish();
            owner_check("PASS owner ACK/deny; exact-FD foreign, same-UID and fork child rejected without mutation");
        }
    }
    setup(); assert(request(&foreign_lwp, VT_SETMODE, &automatic) == EPERM);
    assert(screen.cookie == sd && !callbacks && !frees);
    assert(request(&owner_lwp, VT_SETMODE, &automatic) == 0);
    assert(!screen.cookie); advance(1); assert(frees == 1);
    owner_check("PASS only owner can remove its lease without administrative authorization");

    setup();
    struct vt_mode query = { 0 };
    assert(request(&foreign_lwp, VT_GETMODE, &query) == 0);
    assert(query.mode == VT_PROCESS && query.relsig == 10 && query.acqsig == 12);
    finish(); owner_check("PASS GETMODE remains readable by another FD holder");

    setup(); ack = VT_TRUE;
    auth_error = EACCES; /* The owner's identity survives credential changes. */
    owner_lwp.l_cred = &other_cred;
    assert(usl_detachproc(sd, 0, completed, &screen) == EAGAIN);
    assert(request(&owner_lwp, VT_RELDISP, &ack) == 0);
    owner_lwp.l_cred = &ordinary_cred; auth_error = 0;
    finish(); owner_check("PASS owner permission follows the process, not saved credentials or UID");

    for (int attach = 0; attach < 2; attach++) {
        setup(); ack = attach ? VT_ACKACQ : VT_TRUE;
        assert((attach ? usl_attachproc : usl_detachproc)(sd, 0, completed, &screen) == EAGAIN);
        assert(request(&admin_lwp, VT_RELDISP, &ack) == 0);
        assert(callbacks == 1 && !result); finish();
        owner_check("PASS kauth-authorized administrator can acknowledge a live lease");
    }
    setup();
    assert(request(&admin_lwp, VT_SETMODE, &automatic) == 0);
    advance(1); assert(frees == 1);
    owner_check("PASS administrator can reset a lease explicitly");

    setup(); auth_error = EACCES;
    assert(request(&admin_lwp, VT_SETMODE, &automatic) == EPERM);
    assert(screen.cookie == sd && !callbacks && !frees);
    auth_error = 0; finish();
    owner_check("PASS failed kauth authorization cannot grant administrative reset");

    setup(); foreign_lwp.l_cred = NULL;
    assert(request(&foreign_lwp, VT_SETMODE, &automatic) == EPERM);
    foreign_lwp.l_cred = &ordinary_cred; finish();
    owner_check("PASS absent fixture credentials do not imply administration");

    setup();
    assert(request(&foreign_lwp, VT_SETMODE, &controlled) == EBUSY);
    assert(request(&owner_lwp, VT_SETMODE, &controlled) == EBUSY);
    assert(request(&admin_lwp, VT_SETMODE, &controlled) == EBUSY);
    assert(screen.cookie == sd && hook_count == 1);
    int before = frees;
    usl_sync_destroy(sd); advance(1); assert(frees == before + 1);
    owner_check("PASS PROCESS retains live-owner EBUSY even for owner or administrator");

    setup(); usl_sync_destroy(sd); advance(1); frees = 0;
    assert(request(&foreign_lwp, VT_SETMODE, &controlled) == 0);
    sd = screen.cookie;
    assert(sd->s_proc == &foreign);
    assert(usl_detachproc(sd, 0, completed, &screen) == EAGAIN);
    ack = VT_TRUE; assert(request(&foreign_lwp, VT_RELDISP, &ack) == 0);
    finish(); owner_check("PASS initial unclaimed-screen registration retains its existing FD ABI");

    setup(); fail_hook = 1;
    struct usl_syncdata *new_cookie = NULL;
    assert(usl_sync_init(&screen, &new_cookie, &foreign, 12, 10, 0) == ENOMEM);
    assert(!new_cookie && screen.cookie == sd && hook_count == 1);
    fail_hook = 0; before = frees;
    usl_sync_destroy(sd); advance(1); assert(frees == before + 1);
    owner_check("PASS exit-hook allocation failure rolls back before publication");

    setup(); doexithooks(&foreign);
    assert(screen.cookie == sd && !callbacks);
    assert(usl_sync_check(sd) == 1); finish();
    owner_check("PASS another process exit does not invalidate the owner");

    for (int attach = 0; attach < 2; attach++) {
        setup();
        assert((attach ? usl_attachproc : usl_detachproc)(sd, 0, completed, &screen) == EAGAIN);
        doexithooks(&process); /* Reuse the exact proc address and PID afterwards. */
        assert(!callbacks && !signals && !frees && screen.cookie == sd);
        ack = attach ? VT_ACKACQ : VT_TRUE;
        assert(request(&owner_lwp, VT_RELDISP, &ack) == EPERM);
        assert(usl_sync_check(sd) == 0);
        assert(!screen.cookie && callbacks == 1 && !signals);
        assert(result == (attach ? ENXIO : 0));
        advance(1); assert(frees == 1);
        owner_check("PASS retained FD and reused PID/proc address cannot resurrect a dead lease");
    }
    setup(); doexithooks(&process);
    assert(screen.cookie == sd && !callbacks && !frees);
    advance(1); assert(!screen.cookie && !frees);
    advance(1); assert(frees == 1);
    owner_check("PASS idle owner death unpublishes in deferred context without inline callbacks");

    setup(); ack = VT_TRUE;
    assert(usl_detachproc(sd, 0, completed, &screen) == EAGAIN);
    doexithooks(&process);
    assert(request(&admin_lwp, VT_RELDISP, &ack) == 0);
    assert(callbacks == 1 && !result);
    advance(1); assert(!screen.cookie && !frees);
    advance(1); assert(frees == 1 && callbacks == 1);
    owner_check("PASS administrative ACK cannot cancel deferred dead-owner cleanup");

    setup(); doexithooks(&process);
    assert(request(&owner_lwp, VT_SETMODE, &controlled) == 0);
    assert(screen.cookie != sd && hook_count == 2);
    sd = screen.cookie;
    advance(1); assert(frees == 1 && screen.cookie == sd && hook_count == 1);
    frees = 0; finish();
    owner_check("PASS reused PID/proc can acquire a fresh lease without old cleanup removing it");

    for (int lock = 0; lock < 2; lock++) {
        setup(); hook_lock = lock ? &process_lock : &proc_lock;
        lock_hook = exit_owner;
        assert(usl_detachproc(sd, 0, completed, &screen) == EAGAIN);
        advance(1); assert(!signals && !screen.cookie && callbacks == 1 && !result);
        advance(1); assert(frees == 1);
        owner_check("PASS owner death during process-lock acquisition prevents a stale signal and completes once");
    }
    setup(); writer_hook = retirement_exit;
    usl_sync_destroy(sd); advance(1);
    assert(!writer_hook && frees == 1 && !callbacks);
    doexithooks(&process); /* All hook callbacks have been removed before free. */
    owner_check("PASS DYING cookie stays alive through exit-hook retirement drain");

    setup(); auth_hook = auth_destroy;
    assert(request(&admin_lwp, VT_SETMODE, &automatic) == EINVAL);
    assert(!frees && !screen.cookie);
    advance(1); assert(frees == 1);
    owner_check("PASS ioctl pin and post-kauth DYING check survive an authorization lock wait");

    printf("PASS %u USL owner/lifetime boundary cases\n", tests);
}
