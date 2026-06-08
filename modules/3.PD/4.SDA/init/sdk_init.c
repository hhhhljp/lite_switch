#include <stdio.h>
#include <string.h>
#include <time.h>

#include "sda_core.h"
#include "sdk_init.h"

#ifndef SDA_NO_HW

#include "fm_sdk.h"
#include "fm_support_root.h"

extern fm_status fmPlatformPortInitialize(fm_int sw);

#define SDK_SWITCH_TIMEOUT_SEC  60

static fm_semaphore g_insert_sem;

static void event_handler(fm_int event, fm_int sw, void *arg)
{
    fm_status st;
    (void)arg;

    switch (event) {
    case FM_EVENT_SWITCH_INSERTED:
        fprintf(stderr, "sdk: switch %d inserted\n", sw);
        st = fmSupportInitialize(sw);
        if (st != FM_OK)
            fprintf(stderr, "sdk: fmSupportInitialize(%d) failed: %s\n",
                    sw, fmErrorMsg(st));
        fmReleaseSemaphore(&g_insert_sem);
        break;
    case FM_EVENT_SWITCH_REMOVED:
        fprintf(stderr, "sdk: switch %d removed\n", sw);
        break;
    default:
        break;
    }
}

static void sdk_wait_switch_up(fm_int sw)
{
    fm_int          poll_remaining;
    fm_switchState  ext_state;
    fm_switchInfo   info;
    struct timespec ts;
    fm_status       st;

    fprintf(stderr, "sdk: fmSetSwitchState(%d, 1)...\n", sw);
    st = fmSetSwitchState(sw, 1);
    if (st != FM_OK)
        fprintf(stderr, "sdk: fmSetSwitchState(%d) failed: %s\n",
                sw, fmErrorMsg(st));

    poll_remaining = SDK_SWITCH_TIMEOUT_SEC;
    while (poll_remaining > 0) {
        ext_state = FM_SWITCH_STATE_UNKNOWN;
        if (fmGetSwitchStateExt(sw, &ext_state) == FM_OK
            && ext_state == FM_SWITCH_STATE_UP) {
            memset(&info, 0, sizeof(info));
            fmGetSwitchInfo(sw, &info);
            fprintf(stderr, "sdk: switch %d UP (%d ports)\n",
                    sw, (int)info.numPorts);
            return;
        }
        if (poll_remaining % 10 == 0)
            fprintf(stderr, "sdk: switch %d state=%d, waiting...\n",
                    sw, (int)ext_state);
        ts.tv_sec = 0; ts.tv_nsec = 500000000L;
        nanosleep(&ts, NULL);
        poll_remaining--;
    }
    fprintf(stderr, "sdk: timed out waiting for switch %d UP (last state=%d)\n",
            sw, (int)ext_state);
}

int sdk_init_all(void)
{
    fm_status     st;
    fm_timestamp  timeout;
    fm_int        sw, next_sw, found;
    fm_switchInfo info;

    fprintf(stderr, "sdk: fmOSInitialize...\n");
    st = fmOSInitialize();
    if (st != FM_OK) { fprintf(stderr, "sdk: fmOSInitialize failed: %s\n",
                                fmErrorMsg(st)); return SDA_ERR; }

    fprintf(stderr, "sdk: creating semaphore...\n");
    st = fmCreateSemaphore("sda_insert", FM_SEM_COUNTING, &g_insert_sem, 0);
    if (st != FM_OK) { fprintf(stderr, "sdk: fmCreateSemaphore failed: %s\n",
                                fmErrorMsg(st)); return SDA_ERR; }

    fprintf(stderr, "sdk: fmInitialize...\n");
    st = fmInitialize(event_handler);
    if (st != FM_OK) { fprintf(stderr, "sdk: fmInitialize failed: %s\n",
                                fmErrorMsg(st)); return SDA_ERR; }

    st = fmSetProcessEventMask(0xffffffff);
    if (st != FM_OK) { fprintf(stderr, "sdk: fmSetProcessEventMask failed: %s\n",
                                fmErrorMsg(st)); return SDA_ERR; }

    fprintf(stderr, "sdk: switch count = %d (SDA_NUM_SWITCHES)\n",
            SDA_NUM_SWITCHES);

    st = fmSupportBeforeInitialize(0);
    if (st != FM_OK) { fprintf(stderr, "sdk: fmSupportBeforeInitialize(0) failed: %s\n",
                                fmErrorMsg(st)); return SDA_ERR; }

    timeout.sec = SDK_SWITCH_TIMEOUT_SEC; timeout.usec = 0;
    found = 0;
    st = fmGetSwitchFirst(&sw);
    while (st == FM_OK && sw >= 0) {
        found++;
        memset(&info, 0, sizeof(info));
        if (fmGetSwitchInfo(sw, &info) != FM_OK)
            memset(&info, 0, sizeof(info));
        if (info.up) {
            fprintf(stderr, "sdk: switch %d already UP, initializing...\n", sw);
            st = fmSupportInitialize(sw);
            if (st != FM_OK) { fprintf(stderr, "sdk: fmSupportInitialize(%d) failed: %s\n",
                                        sw, fmErrorMsg(st)); return SDA_ERR; }
        }
        st = fmGetSwitchNext(sw, &next_sw);
        sw = next_sw;
    }

    if (found < SDA_NUM_SWITCHES) {
        fprintf(stderr, "sdk: waiting for switch 0... (timeout=%ds)\n",
                SDK_SWITCH_TIMEOUT_SEC);
        st = fmCaptureSemaphore(&g_insert_sem, &timeout);
        if (st != FM_OK) { fprintf(stderr, "sdk: timed out waiting for switch 0: %s\n",
                                    fmErrorMsg(st)); return SDA_ERR; }
    }

    fprintf(stderr, "sdk: bringing switch 0 UP... (timeout=%ds)\n",
            SDK_SWITCH_TIMEOUT_SEC);
    sdk_wait_switch_up(0);

    fprintf(stderr, "sdk: fmPlatformPortInitialize(0)...\n");
    st = fmPlatformPortInitialize(0);
    if (st != FM_OK) { fprintf(stderr, "sdk: fmPlatformPortInitialize(0) failed: %s\n",
                                fmErrorMsg(st)); return SDA_ERR; }

    fprintf(stderr, "sdk: initialization complete (%d switch(es))\n",
            SDA_NUM_SWITCHES);
    return SDA_OK;
}

void sdk_cleanup(void)
{
    fmDeleteSemaphore(&g_insert_sem);
}

#else /* SDA_NO_HW */

int sdk_init_all(void)
{
    fprintf(stderr, "sdk: NO_HW mode, skipping init\n");
    return SDA_OK;
}

void sdk_cleanup(void) { /* nothing */ }

#endif /* SDA_NO_HW */