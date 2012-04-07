/************************************************************************************
 *
 *  Copyright (C) 2009-2011 Broadcom Corporation
 *
 *  This program is the proprietary software of Broadcom Corporation and/or its
 *  licensors, and may only be used, duplicated, modified or distributed
 *  pursuant to the terms and conditions of a separate, written license
 *  agreement executed between you and Broadcom (an "Authorized License").
 *  Except as set forth in an Authorized License, Broadcom grants no license
 *  (express or implied), right to use, or waiver of any kind with respect to
 *  the Software, and Broadcom expressly reserves all rights in and to the
 *  Software and all intellectual property rights therein.
 *  IF YOU HAVE NO AUTHORIZED LICENSE, THEN YOU HAVE NO RIGHT TO USE THIS
 *  SOFTWARE IN ANY WAY, AND SHOULD IMMEDIATELY NOTIFY BROADCOM AND DISCONTINUE
 *  ALL USE OF THE SOFTWARE.
 *
 *  Except as expressly set forth in the Authorized License,
 *
 *  1.     This program, including its structure, sequence and organization,
 *         constitutes the valuable trade secrets of Broadcom, and you shall
 *         use all reasonable efforts to protect the confidentiality thereof,
 *         and to use this information only in connection with your use of
 *         Broadcom integrated circuit products.
 *
 *  2.     TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED
 *         "AS IS" AND WITH ALL FAULTS AND BROADCOM MAKES NO PROMISES,
 *         REPRESENTATIONS OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY,
 *         OR OTHERWISE, WITH RESPECT TO THE SOFTWARE.  BROADCOM SPECIFICALLY
 *         DISCLAIMS ANY AND ALL IMPLIED WARRANTIES OF TITLE, MERCHANTABILITY,
 *         NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF VIRUSES,
 *         ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 *         CORRESPONDENCE TO DESCRIPTION. YOU ASSUME THE ENTIRE RISK ARISING OUT
 *         OF USE OR PERFORMANCE OF THE SOFTWARE.
 *
 *  3.     TO THE MAXIMUM EXTENT PERMITTED BY LAW, IN NO EVENT SHALL BROADCOM OR
 *         ITS LICENSORS BE LIABLE FOR
 *         (i)   CONSEQUENTIAL, INCIDENTAL, SPECIAL, INDIRECT, OR EXEMPLARY
 *               DAMAGES WHATSOEVER ARISING OUT OF OR IN ANY WAY RELATING TO
 *               YOUR USE OF OR INABILITY TO USE THE SOFTWARE EVEN IF BROADCOM
 *               HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES; OR
 *         (ii)  ANY AMOUNT IN EXCESS OF THE AMOUNT ACTUALLY PAID FOR THE
 *               SOFTWARE ITSELF OR U.S. $1, WHICHEVER IS GREATER. THESE
 *               LIMITATIONS SHALL APPLY NOTWITHSTANDING ANY FAILURE OF
 *               ESSENTIAL PURPOSE OF ANY LIMITED REMEDY.
 *
 ************************************************************************************/


/************************************************************************************
 *
 *  Filename:      btif_hf.c
 *
 *  Description:   Handsfree Profile Bluetooth Interface
 *
 *
 ***********************************************************************************/
#include <hardware/bluetooth.h>
#include <hardware/bt_sock.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <cutils/sockets.h>
#include <netinet/tcp.h>


#define LOG_TAG "BTIF_SOCK"
#include "btif_common.h"
#include "btif_util.h"

#include "bd.h"

#include "bta_api.h"
#include "btif_sock_thread.h"
#include "btif_sock_sdp.h"

#include "bt_target.h"
#include "gki.h"
#include "hcimsgs.h"
#include "sdp_api.h"
#include "btu.h"
#include "btm_api.h"
#include "btm_int.h"
#include "bta_jv_api.h"
#include "bta_jv_co.h"
#include "port_api.h"

#include <cutils/log.h>

#define info(fmt, ...)  ALOGI ("%s: " fmt,__FUNCTION__,  ## __VA_ARGS__)
#define debug(fmt, ...) ALOGD ("%s: " fmt,__FUNCTION__,  ## __VA_ARGS__)
#define error(fmt, ...) ALOGE ("## ERROR : %s: " fmt "##",__FUNCTION__,  ## __VA_ARGS__)
#define asrt(s) if(!(s)) ALOGE ("## %s assert %s failed at line:%d ##",__FUNCTION__, #s, __LINE__)


int sock_send_all(int sock_fd, const uint8_t* buf, int len)
{
    int s = len;
    int ret;
    while(s)
    {
        do ret = send(sock_fd, buf, s, 0);
        while(ret < 0 && errno == EINTR);
        if(ret <= 0)
        {
            error("sock fd:%d send errno:%d, ret:%d", sock_fd, errno, ret);
            return -1;
        }
        buf += ret;
        s -= ret;
    }
    return len;
}
int sock_recv_all(int sock_fd, uint8_t* buf, int len)
{
    int r = len;
    int ret = -1;
    while(r)
    {
        do ret = recv(sock_fd, buf, r, MSG_WAITALL);
        while(ret < 0 && errno == EINTR);
        if(ret <= 0)
        {
            error("sock fd:%d recv errno:%d, ret:%d", sock_fd, errno, ret);
            return -1;
        }
        buf += ret;
        r -= ret;
    }
    return len;
}

int sock_send_fd(int sock_fd, const uint8_t* buf, int len, int send_fd)
{
    ssize_t ret;
    struct msghdr msg;
    unsigned char *buffer = (unsigned char *)buf;
    memset(&msg, 0, sizeof(msg));

    struct cmsghdr *cmsg;
    char msgbuf[CMSG_SPACE(1)];
    asrt(send_fd != -1);
    if(sock_fd == -1 || send_fd == -1)
        return -1;
    // Add any pending outbound file descriptors to the message
           // See "man cmsg" really
    msg.msg_control = msgbuf;
    msg.msg_controllen = sizeof msgbuf;
    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof send_fd);
    memcpy(CMSG_DATA(cmsg), &send_fd, sizeof send_fd);

    // We only write our msg_control during the first write
    int ret_len = len;
    while (len > 0) {
        struct iovec iv;
        memset(&iv, 0, sizeof(iv));

        iv.iov_base = buffer;
        iv.iov_len = len;

        msg.msg_iov = &iv;
        msg.msg_iovlen = 1;

        do {
            ret = sendmsg(sock_fd, &msg, MSG_NOSIGNAL);
        } while (ret < 0 && errno == EINTR);

        if (ret < 0) {
            error("fd:%d, send_fd:%d, sendmsg ret:%d, errno:%d, %s",
                     sock_fd, send_fd, (int)ret, errno, strerror(errno));
            ret_len = -1;
            break;
        }

        buffer += ret;
        len -= ret;

        // Wipes out any msg_control too
        memset(&msg, 0, sizeof(msg));
    }
    debug("close fd:%d after sent", send_fd);
    close(send_fd);
    return ret_len;
}


#define PRINT(s) __android_log_write(ANDROID_LOG_DEBUG, NULL, s)
static const char* hex_table = "0123456789abcdef";
static inline void byte2hex(const char* data, char** str)
{
    **str = hex_table[(*data >> 4) & 0xf];
    ++*str;
    **str = hex_table[*data & 0xf];
    ++*str;
}
static inline void byte2char(const char* data, char** str)
{
    **str = *data < ' ' ? '.' : *data > '~' ? '.' : *data;
    ++(*str);
}
static inline void word2hex(const char* data, char** hex)
{
    byte2hex(&data[1], hex);
    byte2hex(&data[0], hex);
}
void dump_bin(const char* title, const char* data, int size)
{
    char line_buff[256];
    char *line;
    int i, j, addr;
    const int width = 16;
    ALOGD("%s, size:%d, dump started {", title, size);
    if(size <= 0)
        return;
    //write offset
    line = line_buff;
    *line++ = ' ';
    *line++ = ' ';
    *line++ = ' ';
    *line++ = ' ';
    *line++ = ' ';
    *line++ = ' ';
    for(j = 0; j < width; j++)
    {
        byte2hex((const char*)&j, &line);
        *line++ = ' ';
    }
    *line = 0;
    PRINT(line_buff);

    for(i = 0; i < size / width; i++)
    {
        line = line_buff;
        //write address:
        addr = i*width;
        word2hex((const char*)&addr, &line);
        *line++ = ':'; *line++ = ' ';
        //write hex of data
        for(j = 0; j < width; j++)
        {
            byte2hex(&data[j], &line);
            *line++ = ' ';
        }
        //write char of data
        for(j = 0; j < width; j++)
            byte2char(data++, &line);
        //wirte the end of line
        *line = 0;
        //output the line
        PRINT(line_buff);
    }
    //last line of left over if any
    int leftover = size % width;
    if(leftover > 0)
    {
        line = line_buff;
        //write address:
        addr = i*width;
        word2hex((const char*)&addr, &line);
        *line++ = ':'; *line++ = ' ';
        //write hex of data
        for(j = 0; j < leftover; j++) {
            byte2hex(&data[j], &line);
            *line++ = ' ';
        }
        //write hex padding
        for(; j < width; j++) {
            *line++ = ' ';
            *line++ = ' ';
            *line++ = ' ';
        }
        //write char of data
        for(j = 0; j < leftover; j++)
            byte2char(data++, &line);
        //write the end of line
        *line = 0;
        //output the line
        PRINT(line_buff);
    }
    ALOGD("%s, size:%d, dump ended }", title, size);
}

