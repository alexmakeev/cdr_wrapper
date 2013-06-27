#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <limits.h>

#include <dlfcn.h>

#include "misc_macros.h"
#include "misclib.h"

#include "cx_sysdeps.h"

#include "cxlib.h"
#include "Cdr.h"
#include "Knobs_typesP.h"


static char progname[40] = "";

static void reporterror(const char *format, ...)
    __attribute__ ((format (printf, 1, 2)));

static void reporterror(const char *format, ...)
{
  va_list ap;

    va_start(ap, format);
#if 1
    fprintf (stderr, "%s %s%s ",
             strcurtime(), progname, progname[0] != '\0' ? ": " : "");
    vfprintf(stderr, format, ap);
    fprintf (stderr, "\n");
#endif
    va_end(ap);
}


//// Slotarrays management ///////////////////////////////////////////

enum {NUMLOCALREGS = 1000};

typedef struct
{
  int             in_use;
  char            subsysname[200];
  void           *handle;
  subsysdescr_t  *info;
  cda_serverid_t  mainsid;
  groupelem_t    *grouplist;
  double          localregs      [NUMLOCALREGS];
  char            localregsinited[NUMLOCALREGS];

  int             frs_cid;
  int             frs_bid;
} simplesubsys_t;

enum
{
    SUBSYS_MAX       = 0,
    SUBSYS_ALLOC_INC = 2,     // Must be >1 (to provide growth from 0 to 2)
};

static simplesubsys_t *subsys_list        = NULL;
static int             subsys_list_allocd = 0;

// GetSubsysSlot()
GENERIC_SLOTARRAY_DEFINE_GROWING(static, Subsys, simplesubsys_t,
                                 subsys, in_use, 0, 1,
                                 1, SUBSYS_ALLOC_INC, SUBSYS_MAX,
                                 , , void)

static void RlsSubsysSlot(int yid)
{
  simplesubsys_t *syp = AccessSubsysSlot(yid);
  int             err = errno;        // To preserve errno

    if (yid < 0  ||  yid >= subsys_list_allocd  ||  syp->in_use == 0) return;

    if (syp->grouplist != NULL)               CdrDestroyGrouplist(syp->grouplist);
    if (syp->mainsid   != CDA_SERVERID_ERROR) cda_del_server(syp->mainsid);
    if (syp->handle    != NULL)               dlclose(syp->handle);

    syp->in_use = 0;

    errno = err;
}

//--------------------------------------------------------------------

typedef struct
{
    int                      in_use;
    int                      yid;
    const char              *name;
    Knob                     k;
    CdrSimpleChanNewValCB_t  cb;
    void                    *privptr;
    int                      nxt_cid;
} simplechan_t;

enum
{
    SMPLCH_MAX       = 0,
    SMPLCH_ALLOC_INC = 2,     // Must be >1 (to provide growth from 0 to 2)
};

static simplechan_t *smplch_list        = NULL;
static int           smplch_list_allocd = 0;

// GetSmplchSlot()
GENERIC_SLOTARRAY_DEFINE_GROWING(static, Smplch, simplechan_t,
                                 smplch, in_use, 0, 1,
                                 1, SMPLCH_ALLOC_INC, SMPLCH_MAX,
                                 , , void)

static void RlsSmplchSlot(int cid)
{
  simplechan_t *scp = AccessSmplchSlot(cid);

    safe_free(scp->name);
    scp->in_use = 0;
}

//--------------------------------------------------------------------

typedef struct
{
    int                      in_use;
    int                      yid;
    const char              *name;
    Knob                     k;
    CdrSimpleChanNewBigCB_t  cb;
    void                    *privptr;
    int                      nxt_bid;
    //
    cda_serverid_t           bigc_sid;
    cda_bigchandle_t         bigc_handle;
    uint8                   *databuf;
} splbigchan_t;

enum
{
    SBIGCH_MAX       = 0,
    SBIGCH_ALLOC_INC = 2,     // Must be >1 (to provide growth from 0 to 2)
};

static splbigchan_t *sbigch_list        = NULL;
static int           sbigch_list_allocd = 0;

// GetSbigchSlot()
GENERIC_SLOTARRAY_DEFINE_GROWING(static, Sbigch, splbigchan_t,
                                 sbigch, in_use, 0, 1,
                                 1, SBIGCH_ALLOC_INC, SBIGCH_MAX,
                                 , , void)

static void RlsSbigchSlot(int bid)
{
  splbigchan_t *sbp = AccessSbigchSlot(bid);

    safe_free(sbp->name);
    safe_free(sbp->databuf);
    sbp->in_use = 0;
}

//// Subsystem operation /////////////////////////////////////////////

static void EventProc(cda_serverid_t sid __attribute__((unused)), int reason, void *privptr)
{
  int                 yid  = ptr2lint(privptr);
  simplesubsys_t     *syp = AccessSubsysSlot(yid);
  cda_localreginfo_t  localreginfo;

  int                 cid;
  simplechan_t       *scp;

    localreginfo.count       = NUMLOCALREGS;
    localreginfo.regs        = syp->localregs;
    localreginfo.regsinited  = syp->localregsinited;

    CdrProcessGrouplist(reason, 0, NULL, &localreginfo, syp->grouplist);

    for (cid = syp->frs_cid;
         cid >= 0;
         cid = scp->nxt_cid)
    {
        scp = AccessSmplchSlot(cid);
        if (scp->cb != NULL)
            scp->cb(cid, scp->k->curv, scp->privptr);
    }
}

static int subsysname_checker(simplesubsys_t *syp, void *privptr)
{
  const char *subsysname = privptr;

    return strcasecmp(subsysname, syp->subsysname) == 0;
}
static int GetSubsysID(const char *argv0,
                       const char *caller,
                       const char *subsysname)
{
  int             yid;
  simplesubsys_t *syp;
  char           *err;

    /* Check if this subsystem is already loaded */
    yid = ForeachSubsysSlot(subsysname_checker, subsysname);
    if (yid >= 0) return yid;

    /* No, should load... */

    /* First, allocate... */
    yid = GetSubsysSlot();
    if (yid < 0)
    {
        reporterror("%s: unable to allocate subsys-slot", caller);
        return -1;
    }
    syp = AccessSubsysSlot(yid);
    strzcpy(syp->subsysname, subsysname, sizeof(syp->subsysname));
    syp->mainsid = CDA_SERVERID_ERROR;
    syp->frs_cid = -1;
    syp->frs_bid = -1;

    /* ...than open... */
    if (CdrOpenDescription(subsysname, argv0, &(syp->handle), &(syp->info), &err) != 0)
    {
        reporterror("%s: OpenDescription(\"%s\"): %s",
                    caller, subsysname, err);
        goto ERREXIT;
    }
    /* ...and use */
    syp->mainsid = cda_new_server(syp->info->defserver,
                                  EventProc, lint2ptr(yid),
                                  CDA_REGULAR);
    if (syp->mainsid == CDA_SERVERID_ERROR)
    {
        reporterror("%s: cda_new_server(\"%s\"): %s",
                    caller, syp->info->defserver, cx_strerror(errno));
        goto ERREXIT;
    }
    if (syp->info->phys_info_count < 0)
    {
        cda_TMP_register_physinfo_dbase((physinfodb_rec_t *)(syp->info->phys_info));
    }
    else
    {
        cda_TMP_register_physinfo_dbase(NULL);
        cda_set_physinfo(syp->mainsid, syp->info->phys_info, syp->info->phys_info_count);
    }
    syp->grouplist = CdrCvtGroupunits2Grouplist(syp->mainsid, syp->info->grouping);
    if (syp->grouplist == NULL)
    {
        reporterror("%s: CdrCvtGroupunits2Grouplist(): %s",
                    caller, cx_strerror(errno));
        goto ERREXIT;
    }

    cda_run_server(syp->mainsid);

    return yid;

 ERREXIT:
    RlsSubsysSlot(yid);

    return -1;
}

//// Scalar channels support /////////////////////////////////////////

static int channame_checker  (simplechan_t *scp, void *privptr)
{
  const char *name = privptr;

    return strcasecmp(name, scp->name) == 0;
}
int   CdrRegisterSimpleChan(const char *name, const char *argv0,
                            CdrSimpleChanNewValCB_t cb, void *privptr)
{
  const char     *dot_p;
  const char     *k_name;
  char            subsysname[200];
  size_t          subsysnamelen;

  int             yid;
  simplesubsys_t *syp;

  Knob            k;
  int             cid;
  simplechan_t   *scp;

#if OPTION_HAS_PROGRAM_INVOCATION_NAME /* With GNU libc+ld we can determine the true argv[0] */
    if (progname[0] == '\0') strzcpy(progname, program_invocation_short_name, sizeof(progname));
#endif /* OPTION_HAS_PROGRAM_INVOCATION_NAME */

    /* Perform checks */
    if (name == NULL)
    {
        reporterror("%s: NULL channel request", __FUNCTION__);
        return -1;
    }
    if (*name == '\0')
    {
        reporterror("%s: empty channel request", __FUNCTION__);
        return -1;
    }
    dot_p = strchr(name, '.');
    if (dot_p == NULL)
    {
        reporterror("%s: '.'-less channel request \"%s\"", __FUNCTION__, name);
        return -1;
    }
    /* If this was already registered -- just return its id */
    cid = ForeachSmplchSlot(channame_checker, name);
    if (cid >= 0) return cid;

    k_name = dot_p + 1;

    /* Obtain subsys name */
    subsysnamelen = dot_p - name;
    if (subsysnamelen > sizeof(subsysname) - 1)
        subsysnamelen = sizeof(subsysname) - 1;
    memcpy(subsysname, name, subsysnamelen); subsysname[subsysnamelen] = '\0';

    /* ...and reference */
    yid = GetSubsysID(argv0, __FUNCTION__, subsysname);
    if (yid < 0) return -1;
    syp = AccessSubsysSlot(yid);

    k = datatree_FindNode(syp->grouplist, k_name);
    if (k == NULL)
    {
        reporterror("%s: node \"%s\" not found",
                    __FUNCTION__, name);
        return -1;
    }
    if (k->type == LOGT_SUBELEM)
    {
        reporterror("%s: attempt to use subelem (\"%s\")",
                    __FUNCTION__, name);
        return -1;
    }

    cid = GetSmplchSlot();
    if (cid < 0)
    {
        reporterror("%s: unable to allocate chan-slot", __FUNCTION__);
        return -1;
    }
    scp = AccessSmplchSlot(cid);
    if ((scp->name = strdup(name)) == NULL)
    {
        reporterror("%s: unable to allocate chan-slot.name", __FUNCTION__);
        RlsSmplchSlot(cid);
        return -1;
    }

    scp->yid     = yid;
    scp->k       = k;
    scp->cb      = cb;
    scp->privptr = privptr;

    /* Add to the head of callback-queue */
    scp->nxt_cid = syp->frs_cid; syp->frs_cid = cid;

    return cid;
}

int   CdrSetSimpleChanVal  (int handle, double  val)
{
  simplechan_t   *scp = AccessSmplchSlot(handle);
  simplesubsys_t *syp;

  cda_localreginfo_t  localreginfo;

    if (handle < 0  ||  handle >= smplch_list_allocd  ||  scp->in_use == 0)
    {
        reporterror("%s: invalid handle (%d)", __FUNCTION__, handle);
        return -1;
    }

    syp = AccessSubsysSlot(scp->yid);
    localreginfo.count       = NUMLOCALREGS;
    localreginfo.regs        = syp->localregs;
    localreginfo.regsinited  = syp->localregsinited;

    return CdrSetKnobValue(scp->k, val, 0, &localreginfo);
}

int   CdrGetSimpleChanVal  (int handle, double *val_p)
{
  simplechan_t   *scp = AccessSmplchSlot(handle);

    if (handle < 0  ||  handle >= smplch_list_allocd  ||  scp->in_use == 0)
    {
        reporterror("%s: invalid handle (%d)", __FUNCTION__, handle);
        return -1;
    }

    *val_p = scp->k->curv;

    return 0;
}

//// Big-channels support ////////////////////////////////////////////

static void bigc_event_proc(cda_serverid_t  sid       __attribute__((unused)),
                            int             reason,
                            void           *privptr)
{
  int             bid = ptr2lint(privptr);
  splbigchan_t   *sbp = AccessSbigchSlot(bid);

    if (sbp->cb != 0)
        sbp->cb(bid, sbp->privptr);
}

static int bigcname_checker  (splbigchan_t *sbp, void *privptr)
{
  const char *name = privptr;

    return strcasecmp(name, sbp->name) == 0;
}
int   CdrRegisterSimpleBigc(const char *name, const char *argv0,
                            size_t max_datasize,
                            CdrSimpleChanNewBigCB_t cb, void *privptr)
{
  const char     *dot_p;
  const char     *k_name;
  char            subsysname[200];
  size_t          subsysnamelen;

  int             yid;
  simplesubsys_t *syp;

  Knob            k;
  int             bid;
  splbigchan_t   *sbp;

  int             bigc_n;
  const char     *ph_srvref;
  int             ph_chan_n;
  cda_serverid_t  ph_defsid;

#if OPTION_HAS_PROGRAM_INVOCATION_NAME /* With GNU libc+ld we can determine the true argv[0] */
    if (progname[0] == '\0') strzcpy(progname, program_invocation_short_name, sizeof(progname));
#endif /* OPTION_HAS_PROGRAM_INVOCATION_NAME */

    /* Perform checks */
    if (name == NULL)
    {
        reporterror("%s: NULL bigchan request", __FUNCTION__);
        return -1;
    }
    if (*name == '\0')
    {
        reporterror("%s: empty bigchan request", __FUNCTION__);
        return -1;
    }
    dot_p = strchr(name, '.');
    if (dot_p == NULL)
    {
        reporterror("%s: '.'-less bigchan request \"%s\"", __FUNCTION__, name);
        return -1;
    }
    /* If this was already registered -- just return its id */
    bid = ForeachSbigchSlot(bigcname_checker, name);
    if (bid >= 0) return bid;

    k_name = dot_p + 1;

    /* Obtain subsys name */
    subsysnamelen = dot_p - name;
    if (subsysnamelen > sizeof(subsysname) - 1)
        subsysnamelen = sizeof(subsysname) - 1;
    memcpy(subsysname, name, subsysnamelen); subsysname[subsysnamelen] = '\0';

    /* ...and reference */
    yid = GetSubsysID(argv0, __FUNCTION__, subsysname);
    if (yid < 0) return -1;
    syp = AccessSubsysSlot(yid);

    k = datatree_FindNode(syp->grouplist, k_name);
    if (k == NULL)
    {
        reporterror("%s: node \"%s\" not found",
                    __FUNCTION__, name);
        return -1;
    }
    if (k->type == LOGT_SUBELEM)
    {
        reporterror("%s: attempt to use subelem (\"%s\")",
                    __FUNCTION__, name);
        return -1;
    }

    bid = GetSbigchSlot();
    if (bid < 0)
    {
        reporterror("%s: unable to allocate bigc-slot", __FUNCTION__);
        return -1;
    }
    sbp = AccessSbigchSlot(bid);
    if ((sbp->name = strdup(name)) == NULL)
    {
        reporterror("%s: unable to allocate bigc-slot.name", __FUNCTION__);
        RlsSbigchSlot(bid);
        return -1;
    }

    sbp->yid     = yid;
    sbp->k       = k;
    sbp->cb      = cb;
    sbp->privptr = privptr;

    /* Obtain address data... */
    bigc_n = k->color;
    if (k->kind == LOGK_DIRECT  &&
        cda_srcof_physchan(k->physhandle, &ph_srvref, &ph_chan_n) == 1)
    {
        ph_defsid = cda_sidof_physchan(k->physhandle);
    }
    else
    {
        ph_srvref = NULL;
        ph_chan_n = -1;
        ph_defsid = CDA_SERVERID_ERROR;
    }
    /* ...and try to bind */
    sbp->bigc_sid = cda_new_server(ph_srvref,
                                   bigc_event_proc, lint2ptr(bid),
                                   CDA_BIGC);
    if (sbp->bigc_sid == CDA_SERVERID_ERROR)
    {
        reporterror("%s: cda_new_server(server=%s): %s",
                   __FUNCTION__, ph_srvref, cx_strerror(errno));
        goto CLEANUP;
    }
    sbp->bigc_handle = cda_add_bigc(sbp->bigc_sid, bigc_n,
                                    CX_MAX_BIGC_PARAMS, max_datasize,
                                    CX_CACHECTL_SHARABLE, 
                                    CX_BIGC_IMMED_YES);
    cda_run_server(sbp->bigc_sid);

    /* Add to the head of callback-queue */
    sbp->nxt_bid = syp->frs_bid; syp->frs_bid = bid;

    return bid;

 CLEANUP:
    return -1;
}

int   CdrGetSimpleBigcData (int handle, int byte_ofs, int byte_size, void *buf)
{
  splbigchan_t   *sbp = AccessSbigchSlot(handle);
  int             r;

    if (handle < 0  ||  handle >= sbigch_list_allocd  ||  sbp->in_use == 0)
    {
        reporterror("%s: invalid handle (%d)", __FUNCTION__, handle);
        return -1;
    }

    r = cda_getbigcdata(sbp->bigc_handle, byte_ofs, byte_size, buf);

    return r;
}

int   CdrSetSimpleBigcData (int handle, int byte_ofs, int byte_size, void *buf, int dataunits)
{
  splbigchan_t   *sbp = AccessSbigchSlot(handle);
  int             r;

    if (handle < 0  ||  handle >= sbigch_list_allocd  ||  sbp->in_use == 0)
    {
        reporterror("%s: invalid handle (%d)", __FUNCTION__, handle);
        return -1;
    }

    r = cda_setbigcdata(sbp->bigc_handle, byte_ofs, byte_size, buf, dataunits);

    return r;
}


int   CdrGetSimpleBigcStats(int handle, int *age_p, int *rflags_p)
{
  splbigchan_t   *sbp = AccessSbigchSlot(handle);
  int             r;
  tag_t           tag;    // Note: these two are of cx-specific types,
  rflags_t        rflags; //       while parameters are just 'int' ("simple"!)

    if (handle < 0  ||  handle >= sbigch_list_allocd  ||  sbp->in_use == 0)
    {
        reporterror("%s: invalid handle (%d)", __FUNCTION__, handle);
        return -1;
    }

    r = cda_getbigcstats(sbp->bigc_handle, &tag, &rflags);
    if (r > 0)
    {
        if (age_p    != NULL) *age_p    = tag;
        if (rflags_p != NULL) *rflags_p = rflags;
    }

    return r;
}


int   CdrGetSimpleBigcParam(int handle, int n, int *val_p)
{
  splbigchan_t   *sbp = AccessSbigchSlot(handle);
  int             r;
  int32           v;

    if (handle < 0  ||  handle >= sbigch_list_allocd  ||  sbp->in_use == 0)
    {
        reporterror("%s: invalid handle (%d)", __FUNCTION__, handle);
        return -1;
    }

    r = cda_getbigcparams(sbp->bigc_handle, n, 1, &v);
    if (r > 0) *val_p = v;

    return r;
}


int   CdrSetSimpleBigcParam(int handle, int n, int  val)
{
  splbigchan_t   *sbp = AccessSbigchSlot(handle);
  int             r;
  int32           v;

    if (handle < 0  ||  handle >= sbigch_list_allocd  ||  sbp->in_use == 0)
    {
        reporterror("%s: invalid handle (%d)", __FUNCTION__, handle);
        return -1;
    }

    v = val;
    r = cda_setbigcparams(sbp->bigc_handle, n, 1, &v);

    return r;
}

//////////////////////////////////////////////////////////////////////

