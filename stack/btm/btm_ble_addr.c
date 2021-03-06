/******************************************************************************
 *
 *  Copyright (C) 1999-2012 Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

/******************************************************************************
 *
 *  This file contains functions for BLE address management.
 *
 ******************************************************************************/

#include <string.h>

#include "bt_types.h"
#include "hcimsgs.h"
#include "btu.h"
#include "btm_int.h"
#include "gap_api.h"

#if (defined BLE_INCLUDED && BLE_INCLUDED == TRUE)
#include "btm_ble_int.h"
#include "smp_api.h"

#include "vendor_ble.h"

/*******************************************************************************
**
** Function         btm_gen_resolve_paddr_cmpl
**
** Description      This is callback functioin when resolvable private address
**                  generation is complete.
**
** Returns          void
**
*******************************************************************************/
static void btm_gen_resolve_paddr_cmpl(tSMP_ENC *p)
{
    tBTM_LE_RANDOM_CB *p_cb = &btm_cb.ble_ctr_cb.addr_mgnt_cb;
    BTM_TRACE_EVENT ("btm_gen_resolve_paddr_cmpl");

    if (p)
    {
        /* set hash to be LSB of rpAddress */
        p_cb->private_addr[5] = p->param_buf[0];
        p_cb->private_addr[4] = p->param_buf[1];
        p_cb->private_addr[3] = p->param_buf[2];
        /* set it to controller */
        btsnd_hcic_ble_set_random_addr(p_cb->private_addr);

        p_cb->own_addr_type = BLE_ADDR_RANDOM;

        /* start a periodical timer to refresh random addr */
        btu_stop_timer_oneshot(&p_cb->raddr_timer_ent);
#if (BTM_BLE_CONFORMANCE_TESTING == TRUE)
        btu_start_timer_oneshot(&p_cb->raddr_timer_ent, BTU_TTYPE_BLE_RANDOM_ADDR,
                         btm_cb.ble_ctr_cb.rpa_tout);
#else
        btu_start_timer_oneshot(&p_cb->raddr_timer_ent, BTU_TTYPE_BLE_RANDOM_ADDR,
                         BTM_BLE_PRIVATE_ADDR_INT);
#endif
    }
    else
    {
        /* random address set failure */
        BTM_TRACE_DEBUG("set random address failed");
    }
}
/*******************************************************************************
**
** Function         btm_gen_resolve_paddr_low
**
** Description      This function is called when random address has generate the
**                  random number base for low 3 byte bd address.
**
** Returns          void
**
*******************************************************************************/
void btm_gen_resolve_paddr_low(tBTM_RAND_ENC *p)
{
#if (BLE_INCLUDED == TRUE && SMP_INCLUDED == TRUE)
    tBTM_LE_RANDOM_CB *p_cb = &btm_cb.ble_ctr_cb.addr_mgnt_cb;
    tSMP_ENC    output;

    BTM_TRACE_EVENT ("btm_gen_resolve_paddr_low");
    if (p)
    {
        p->param_buf[2] &= (~BLE_RESOLVE_ADDR_MASK);
        p->param_buf[2] |= BLE_RESOLVE_ADDR_MSB;

        p_cb->private_addr[2] = p->param_buf[0];
        p_cb->private_addr[1] = p->param_buf[1];
        p_cb->private_addr[0] = p->param_buf[2];

        /* encrypt with ur IRK */
        if (!SMP_Encrypt(btm_cb.devcb.id_keys.irk, BT_OCTET16_LEN, p->param_buf, 3, &output))
        {
            btm_gen_resolve_paddr_cmpl(NULL);
        }
        else
        {
            btm_gen_resolve_paddr_cmpl(&output);
        }
    }
#endif
}
/*******************************************************************************
**
** Function         btm_gen_resolvable_private_addr
**
** Description      This function generate a resolvable private address.
**
** Returns          void
**
*******************************************************************************/
void btm_gen_resolvable_private_addr (void *p_cmd_cplt_cback)
{
    BTM_TRACE_EVENT ("btm_gen_resolvable_private_addr");
    /* generate 3B rand as BD LSB, SRK with it, get BD MSB */
    if (!btsnd_hcic_ble_rand((void *)p_cmd_cplt_cback))
        btm_gen_resolve_paddr_cmpl(NULL);
}
/*******************************************************************************
**
** Function         btm_gen_non_resolve_paddr_cmpl
**
** Description      This is the callback function when non-resolvable private
**                  function is generated and write to controller.
**
** Returns          void
**
*******************************************************************************/
static void btm_gen_non_resolve_paddr_cmpl(tBTM_RAND_ENC *p)
{
    tBTM_LE_RANDOM_CB *p_cb = &btm_cb.ble_ctr_cb.addr_mgnt_cb;
    tBTM_BLE_ADDR_CBACK *p_cback = p_cb->p_generate_cback;
    void    *p_data = p_cb->p;
    UINT8   *pp;
    BD_ADDR     static_random;

    BTM_TRACE_EVENT ("btm_gen_non_resolve_paddr_cmpl");

    p_cb->p_generate_cback = NULL;
    if (p)
    {

        pp = p->param_buf;
        STREAM_TO_BDADDR(static_random, pp);
        /* mask off the 2 MSB */
        static_random[0] &= BLE_STATIC_PRIVATE_MSB_MASK;

        /* report complete */
        if (p_cback)
            (* p_cback)(static_random, p_data);
    }
    else
    {
        BTM_TRACE_DEBUG("btm_gen_non_resolvable_private_addr failed");
        if (p_cback)
            (* p_cback)(NULL, p_data);
    }
}
/*******************************************************************************
**
** Function         btm_gen_non_resolvable_private_addr
**
** Description      This function generate a non-resolvable private address.
**
**
** Returns          void
**
*******************************************************************************/
void btm_gen_non_resolvable_private_addr (tBTM_BLE_ADDR_CBACK *p_cback, void *p)
{
    tBTM_LE_RANDOM_CB   *p_mgnt_cb = &btm_cb.ble_ctr_cb.addr_mgnt_cb;

    BTM_TRACE_EVENT ("btm_gen_non_resolvable_private_addr");

    if (p_mgnt_cb->p_generate_cback != NULL)
        return;

    p_mgnt_cb->p_generate_cback = p_cback;
    p_mgnt_cb->p                = p;
    if (!btsnd_hcic_ble_rand((void *)btm_gen_non_resolve_paddr_cmpl))
    {
        btm_gen_non_resolve_paddr_cmpl(NULL);
    }

}
    #if SMP_INCLUDED == TRUE
/*******************************************************************************
**  Utility functions for Random address resolving
*******************************************************************************/
/*******************************************************************************
**
** Function         btm_ble_resolve_address_cmpl
**
** Description      This function sends the random address resolving complete
**                  callback.
**
** Returns          None.
**
*******************************************************************************/
static void btm_ble_resolve_address_cmpl(void)
{
    tBTM_LE_RANDOM_CB   *p_mgnt_cb = &btm_cb.ble_ctr_cb.addr_mgnt_cb;
    tBTM_SEC_DEV_REC    *p_dev_rec = NULL;

    BTM_TRACE_EVENT ("btm_ble_resolve_address_cmpl p_mgnt_cb->index = %d", p_mgnt_cb->index);

    if (p_mgnt_cb->index < BTM_SEC_MAX_DEVICE_RECORDS)
    {
        p_dev_rec = &btm_cb.sec_dev_rec[p_mgnt_cb->index];
    }

    p_mgnt_cb->busy = FALSE;

    (* p_mgnt_cb->p_resolve_cback)(p_dev_rec, p_mgnt_cb->p);
}
/*******************************************************************************
**
** Function         btm_ble_proc_resolve_x
**
** Description      This function compares the X with random address 3 MSO bytes
**                  to find a match, if not match, continue for next record.
**
** Returns          None.
**
*******************************************************************************/
static BOOLEAN btm_ble_proc_resolve_x(tSMP_ENC *p)
{
    tBTM_LE_RANDOM_CB   *p_mgnt_cb = &btm_cb.ble_ctr_cb.addr_mgnt_cb;
    UINT8    comp[3];
    BTM_TRACE_EVENT ("btm_ble_proc_resolve_x");
    /* compare the hash with 3 LSB of bd address */
    comp[0] = p_mgnt_cb->random_bda[5];
    comp[1] = p_mgnt_cb->random_bda[4];
    comp[2] = p_mgnt_cb->random_bda[3];

    if (p)
    {
        if (!memcmp(p->param_buf, &comp[0], 3))
        {
            /* match is found */
            BTM_TRACE_EVENT ("match is found");
            btm_ble_resolve_address_cmpl();
            return TRUE;
        }
    }
    return FALSE;
}
/*******************************************************************************
**
** Function         btm_ble_match_random_bda
**
** Description      This function match the random address to the appointed device
**                  record, starting from calculating IRK. If record index exceed
**                  the maximum record number, matching failed and send callback.
**
** Returns          None.
**
*******************************************************************************/
static BOOLEAN btm_ble_match_random_bda(UINT16 rec_index)
{
#if (BLE_INCLUDED == TRUE && SMP_INCLUDED == TRUE)
    tBTM_SEC_DEV_REC    *p_dev_rec;
    tBTM_LE_RANDOM_CB   *p_mgnt_cb = &btm_cb.ble_ctr_cb.addr_mgnt_cb;
    UINT8       rand[3];
    tSMP_ENC    output;

    /* use the 3 MSB of bd address as prand */
    rand[0] = p_mgnt_cb->random_bda[2];
    rand[1] = p_mgnt_cb->random_bda[1];
    rand[2] = p_mgnt_cb->random_bda[0];

    BTM_TRACE_EVENT("btm_ble_match_random_bda rec_index = %d", rec_index);

    if (rec_index < BTM_SEC_MAX_DEVICE_RECORDS)
    {
        p_dev_rec = &btm_cb.sec_dev_rec[rec_index];

        BTM_TRACE_DEBUG("sec_flags = %02x device_type = %d", p_dev_rec->sec_flags, p_dev_rec->device_type);

        if ((p_dev_rec->device_type & BT_DEVICE_TYPE_BLE) &&
            (p_dev_rec->ble.key_type & BTM_LE_KEY_PID))
        {
            /* generate X = E irk(R0, R1, R2) and R is random address 3 LSO */
            SMP_Encrypt(p_dev_rec->ble.keys.irk, BT_OCTET16_LEN,
                        &rand[0], 3, &output);
            return btm_ble_proc_resolve_x(&output);
        }
        else
        {
            // not completed
            return FALSE;
        }
    }
    else /* no  match found */
    {
        btm_ble_resolve_address_cmpl();
        return TRUE;
    }
#endif
}

/*******************************************************************************
**
** Function         btm_ble_resolve_random_addr
**
** Description      This function is called to resolve a random address.
**
** Returns          pointer to the security record of the device whom a random
**                  address is matched to.
**
*******************************************************************************/
void btm_ble_resolve_random_addr(BD_ADDR random_bda, tBTM_BLE_RESOLVE_CBACK * p_cback, void *p)
{
    tBTM_LE_RANDOM_CB   *p_mgnt_cb = &btm_cb.ble_ctr_cb.addr_mgnt_cb;

    BTM_TRACE_EVENT ("btm_ble_resolve_random_addr");
    if ( !p_mgnt_cb->busy)
    {
        p_mgnt_cb->p = p;
        p_mgnt_cb->busy = TRUE;
        p_mgnt_cb->index = 0;
        p_mgnt_cb->p_resolve_cback = p_cback;
        memcpy(p_mgnt_cb->random_bda, random_bda, BD_ADDR_LEN);
        /* start to resolve random address */
        /* check for next security record */
        while (TRUE)
        {
            if (btm_ble_match_random_bda(p_mgnt_cb->index))
            {
                /* atch found or went through the list */
                break;
            }
            p_mgnt_cb->index ++;
        }
    }
    else
        (*p_cback)(NULL, p);
}
    #endif
/*******************************************************************************
**  address mapping between pseudo address and real connection address
*******************************************************************************/
/*******************************************************************************
**
** Function         btm_ble_map_bda_to_conn_bda
**
** Description      This function map a BD address to the real connection address
**                  and return the connection address type.
*******************************************************************************/
tBLE_ADDR_TYPE btm_ble_map_bda_to_conn_bda(BD_ADDR bd_addr)
{
    tBTM_SEC_DEV_REC    *p_dev_rec = NULL;
    BTM_TRACE_EVENT ("btm_ble_map_bda_to_conn_bda");
    if ((p_dev_rec = btm_find_dev (bd_addr)) != NULL &&
        p_dev_rec->device_type == BT_DEVICE_TYPE_BLE)
    {
        if (p_dev_rec->ble.ble_addr_type != BLE_ADDR_PUBLIC)
        {
            memcpy(bd_addr, p_dev_rec->ble.static_addr, BD_ADDR_LEN);
        }
        return p_dev_rec->ble.ble_addr_type;
    }
    else
        return BLE_ADDR_PUBLIC;
}

/*******************************************************************************
**
** Function         btm_find_dev_by_public_static_addr
**
** Description      find the security record whose LE static address is matching
**
*******************************************************************************/
tBTM_SEC_DEV_REC* btm_find_dev_by_public_static_addr(BD_ADDR bd_addr)
{
    UINT8               i;
    tBTM_SEC_DEV_REC    *p_dev_rec = &btm_cb.sec_dev_rec[0];
#if BLE_PRIVACY_SPT == TRUE
    for (i = 0; i < BTM_SEC_MAX_DEVICE_RECORDS; i ++, p_dev_rec ++)
    {
        if (p_dev_rec->ble.ble_addr_type == BLE_ADDR_RANDOM &&
            BTM_BLE_IS_RESOLVE_BDA(p_dev_rec->bd_addr))
        {
            if ( memcmp(p_dev_rec->ble.static_addr, bd_addr, BD_ADDR_LEN) == 0)
            {
                p_dev_rec->ble.active_addr_type = BTM_BLE_ADDR_RRA;
                /* found the match */
                return p_dev_rec;
            }
        }
    }
#endif
    return NULL;
}

/*******************************************************************************
**
** Function         btm_public_addr_to_random_pseudo
**
** Description      This function map a static BD address to a pseudo random address
**                  in security database.
**
*******************************************************************************/
BOOLEAN btm_public_addr_to_random_pseudo(BD_ADDR bd_addr, UINT8 *p_addr_type, BOOLEAN read_irk)
{
#if BLE_PRIVACY_SPT == TRUE
    tBTM_SEC_DEV_REC    *p_dev_rec = btm_find_dev_by_public_static_addr(bd_addr);

    BTM_TRACE_EVENT ("btm_public_addr_to_random_pseudo");

    /* evt reported on static address, map static address to random pseudo */
    if (p_dev_rec  != NULL && read_irk &&
        /* static address is not static address */
        memcmp(p_dev_rec->bd_addr, bd_addr, BD_ADDR_LEN) != 0)
        /* update current random */
        btm_ble_read_irk_entry(p_dev_rec->ble.static_addr);
    if (p_dev_rec != NULL)
    {
        /* assign the orginal random to be the current report address */
        memcpy(bd_addr, p_dev_rec->bd_addr, BD_ADDR_LEN);
        /* always be a resolvable random if a match is found */
        *p_addr_type = BLE_ADDR_RANDOM;

        BTM_TRACE_ERROR("matched a public/reconnect address and map to random pseudo");

        return TRUE;
    }
#endif
    return FALSE;
}

/*******************************************************************************
**
** Function         btm_random_pseudo_to_public
**
** Description      This function map a random pseudo address to a public address
**                  random_pseudo is input and output parameter
**
*******************************************************************************/
BOOLEAN btm_random_pseudo_to_public(BD_ADDR random_pseudo, UINT8 *p_static_addr_type)
{
#if BLE_PRIVACY_SPT == TRUE
    tBTM_SEC_DEV_REC    *p_dev_rec = btm_find_dev (random_pseudo);

    if (p_dev_rec != NULL)
    {
        if (p_dev_rec->ble.ble_addr_type == BLE_ADDR_RANDOM &&
            BTM_BLE_IS_RESOLVE_BDA(p_dev_rec->bd_addr) &&
            (p_dev_rec->ble.key_type & BTM_LE_KEY_PID) != 0)
        {
            BTM_TRACE_EVENT ("btm_random_pseudo_to_public found the puclic static address!");
            * p_static_addr_type = p_dev_rec->ble.static_addr_type;
            memcpy(random_pseudo, p_dev_rec->ble.static_addr, BD_ADDR_LEN);
            return TRUE;
        }
    }
#endif
    return FALSE;
}

/*******************************************************************************
**
** Function         btm_ble_refresh_rra
**
** Description      This function refresh the currently used RRA into security
**                  database and set active connection address.
**
*******************************************************************************/
void btm_ble_refresh_rra(BD_ADDR static_bda, BD_ADDR rra)
{
#if BLE_PRIVACY_SPT == TRUE
    tBTM_SEC_DEV_REC    *p_sec_rec = btm_find_dev_by_public_static_addr(static_bda);
    tACL_CONN           *p_acl = btm_bda_to_acl (p_sec_rec->bd_addr, BT_TRANSPORT_LE);
    UINT8               rra_dummy = FALSE;
    BD_ADDR             dummy_bda = {0};

    BTM_TRACE_ERROR("btm_ble_refresh_rra");

    if (memcmp(dummy_bda, rra, BD_ADDR_LEN) == 0)
        rra_dummy = TRUE;

    /* connection refresh RRA */
    if (p_acl != NULL /* && memcmp(p_acl->active_remote_addr, dummy_bda, BD_ADDR_LEN) == 0 */)
    {
        /* use static address, rra is empty */
        if (rra_dummy)
        {
            p_acl->active_remote_addr_type = p_sec_rec->ble.static_addr_type;
            memcpy(p_acl->active_remote_addr, p_sec_rec->ble.static_addr, BD_ADDR_LEN);
        }
        else
        {
            p_acl->active_remote_addr_type = BLE_ADDR_RANDOM;
            memcpy(p_acl->active_remote_addr, rra, BD_ADDR_LEN);
        }
    }
    /* update security record here, in adv event or connection complete process */
    if (p_sec_rec != NULL)
    {
        memcpy(p_sec_rec->ble.cur_rand_addr, rra, BD_ADDR_LEN);
        p_sec_rec->ble.active_addr_type = rra_dummy ? BTM_BLE_ADDR_STATIC: BTM_BLE_ADDR_RRA;
    }
    else
    {
        BTM_TRACE_ERROR("No matching known device in record");
    }
#endif
}
#endif


