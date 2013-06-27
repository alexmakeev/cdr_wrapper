#ifndef __CDR_H
#define __CDR_H


#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */


#include "cxdata.h"
#include "cda.h"
#include "Knobs_types.h"
#include "datatree.h"


enum
{
    CDR_OPT_SYNTHETIC = 1 << 0,
    CDR_OPT_READONLY  = 1 << 1,
};

enum
{
    CDR_OPT_NOLOAD_VALUES = 1 << 31,
    CDR_OPT_NOLOAD_RANGES = 1 << 30,
};


typedef void (*CdrKnobUnhiliter_t)(Knob k);
extern         CdrKnobUnhiliter_t cdr_unhilite_knob_hook;


groupelem_t *CdrCvtGroupunits2Grouplist(cda_serverid_t defsid,
                                        groupunit_t *grouping);
ElemInfo     CdrCvtElemnet2Eleminfo    (cda_serverid_t defsid,
                                        elemnet_t *src);
Knob         CdrCvtLogchannets2Knobs   (cda_serverid_t defsid,
                                        logchannet_t *src, int count,
                                        ElemInfo  holder);

void         CdrDestroyGrouplist(groupelem_t *list);
void         CdrDestroyEleminfo (ElemInfo     info);
void         CdrDestroyKnobs    (Knob         infos, int count);

void         CdrProcessGrouplist(int cause_conn_n, int options, rflags_t *rflags_p,
                                 cda_localreginfo_t *localreginfo,
                                 groupelem_t *list);
void         CdrProcessEleminfo (int cause_conn_n, int options, rflags_t *rflags_p,
                                 cda_localreginfo_t *localreginfo,
                                 ElemInfo info);
void         CdrProcessKnobs    (int cause_conn_n, int options, rflags_t *rflags_p,
                                 cda_localreginfo_t *localreginfo,
                                 Knob         infos, int count);

int          CdrActivateKnobHistory (Knob k, int histring_size);
void         CdrHistorizeKnobsInList(Knob k);

int          CdrSrcOf(Knob k, const char **name_p, int *n_p);

Knob         CdrFindKnob    (groupelem_t *list, const char *name);
Knob         CdrFindKnobFrom(groupelem_t *list, const char *name,
                             ElemInfo     start_point);

int          CdrSetKnobValue(Knob k, double v, int options,
                             cda_localreginfo_t *localreginfo);

int          CdrSaveGrouplistMode(groupelem_t *list, const char *filespec,
                                  const char *subsys, const char *comment);
int          CdrLoadGrouplistMode(groupelem_t *list, const char *filespec,
                                  int options);
int          CdrStatGrouplistMode(const char *filespec,
                                  time_t *cr_time,
                                  char   *commentbuf, size_t commentbuf_size);

int          CdrLogGrouplistMode (groupelem_t *list, const char *filespec,
                                  const char *comment,
                                  int headers, int append,
                                  struct timeval *start);

char *CdrStrcolalarmShort(colalarm_t state);
char *CdrStrcolalarmLong (colalarm_t state);
colalarm_t CdrName2colalarm(char *name);

char *CdrLastErr(void);


/* descraccess API */

int CdrOpenDescription (const char *subsys, const char *argv0,
                        void **handle_p, subsysdescr_t **info_p,
                        char **err_p);
int CdrCloseDescription(void *handle, subsysdescr_t *info);


/* simpleaccess API */

typedef void (*CdrSimpleChanNewValCB_t)(int handle, double val, void *privptr);

int   CdrRegisterSimpleChan(const char *name, const char *argv0,
                            CdrSimpleChanNewValCB_t cb, void *privptr);
int   CdrSetSimpleChanVal  (int handle, double  val);
int   CdrGetSimpleChanVal  (int handle, double *val_p);


typedef void (*CdrSimpleChanNewBigCB_t)(int handle,             void *privptr);

int   CdrRegisterSimpleBigc(const char *name, const char *argv0,
                            size_t max_datasize,
                            CdrSimpleChanNewBigCB_t cb, void *privptr);
int   CdrGetSimpleBigcData (int handle, int byte_ofs, int byte_size, void *buf);
int   CdrSetSimpleBigcData (int handle, int byte_ofs, int byte_size, void *buf, int dataunits);
int   CdrGetSimpleBigcStats(int handle, int *age_p, int *rflags_p);
int   CdrGetSimpleBigcParam(int handle, int n, int *val_p);
int   CdrSetSimpleBigcParam(int handle, int n, int  val);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __CDR_H */
