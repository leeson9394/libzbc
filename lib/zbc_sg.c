/*
 * This file is part of libzbc.
 * 
 * Copyright (C) 2009-2014, HGST, Inc.  This software is distributed
 * under the terms of the GNU Lesser General Public License version 3,
 * or any later version, "as is," without technical support, and WITHOUT
 * ANY WARRANTY, without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  You should have received a copy
 * of the GNU Lesser General Public License along with libzbc.  If not,
 * see <http://www.gnu.org/licenses/>.
 * 
 * Author: Damien Le Moal (damien.lemoal@hgst.com)
 *         Christophe Louargant (christophe.louargant@hgst.com)
 */

/***** Including files *****/

#include "zbc.h"
#include "zbc_sg.h"

#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

/***** Private data *****/

/**
 * Definition of the commands
 * Each command is defined by 3 fields.
 * It's name, it's length, and it's opcode.
 */
static struct zbc_sg_cmd_s
{

    char                *cdb_cmd_name;
    int                 cdb_opcode;
    int                 cdb_sa;
    size_t              cdb_length;

} zbc_sg_cmd_list[ZBC_SG_CMD_NUM] = {

    /* ZBC_INQUIRY */
    {
        "INQUIRY",
        ZBC_SG_INQUIRY_CDB_OPCODE,
        0,
        ZBC_SG_INQUIRY_CDB_LENGTH,
    },

    /* ZBC_READ_CAPACITY */
    {
        "READ CAPACITY 16",
        ZBC_SG_READ_CAPACITY_CDB_OPCODE,
        ZBC_SG_READ_CAPACITY_CDB_SA,
        ZBC_SG_READ_CAPACITY_CDB_LENGTH,
    },

    /* ZBC_READ */
    {
        "READ 16",
        ZBC_SG_READ_CDB_OPCODE,
        0,
        ZBC_SG_READ_CDB_LENGTH,
    },

    /* ZBC_WRITE */
    {
        "WRITE 16",
        ZBC_SG_WRITE_CDB_OPCODE,
        0,
        ZBC_SG_WRITE_CDB_LENGTH,
    },

    /* ZBC_SYNC_CACHE */
    {
        "SYNCHRONIZE CACHE 16",
        ZBC_SG_SYNC_CACHE_CDB_OPCODE,
        0,
        ZBC_SG_SYNC_CACHE_CDB_LENGTH,
    },

    /* ZBC_REPORT_ZONES */
    {
        "REPORT ZONES",
        ZBC_SG_REPORT_ZONES_CDB_OPCODE,
        ZBC_SG_REPORT_ZONES_CDB_SA,
        ZBC_SG_REPORT_ZONES_CDB_LENGTH,
    },

    /* ZBC_RESET_WRITE_POINTER */
    {
        "RESET WRITE POINTER",
        ZBC_SG_RESET_WRITE_POINTER_CDB_OPCODE,
        ZBC_SG_RESET_WRITE_POINTER_CDB_SA,
        ZBC_SG_RESET_WRITE_POINTER_CDB_LENGTH,
    },

    /* ZBC_SET_ZONES */
    {
        "SET ZONES",
        ZBC_SG_SET_ZONES_CDB_OPCODE,
        ZBC_SG_SET_ZONES_CDB_SA,
        ZBC_SG_SET_ZONES_CDB_LENGTH,
    },

    /* ZBC_SET_WRITE_POINTER */
    {
        "SET WRITE POINTER",
        ZBC_SG_SET_WRITE_POINTER_CDB_OPCODE,
        ZBC_SG_SET_WRITE_POINTER_CDB_SA,
        ZBC_SG_SET_WRITE_POINTER_CDB_LENGTH,
    }

};

static char *
zbc_sg_cmd_name(zbc_sg_cmd_t *cmd)
{
    char *name;

    if ( (cmd->code >= 0)
         && (cmd->code < ZBC_SG_CMD_NUM) ) {
        name = zbc_sg_cmd_list[cmd->code].cdb_cmd_name;
    } else {
        name = "(UNKNOWN COMMAND)";
    }

    return( name );

}

/**
 * Free a command.
 */
void
zbc_sg_cmd_destroy(zbc_sg_cmd_t *cmd)
{

    /* Free the command */
    if ( cmd ) {
        if ( cmd->out_buf
             && cmd->out_buf_needfree ) {
            free(cmd->out_buf);
            cmd->out_buf = NULL;
            cmd->out_bufsz = 0;
        }
    }

    return;

}

/**
 * Allocate and initialize a new command.
 */
int
zbc_sg_cmd_init(zbc_sg_cmd_t *cmd,
                int cmd_code,
                uint8_t *out_buf,
                size_t out_bufsz)
{
    int ret = 0;

    if ( (! cmd)
         || (cmd_code < 0)
         || (cmd_code >= ZBC_SG_CMD_NUM) ) {
        zbc_error("Invalid command specified\n");
        return( -EINVAL );
    }

    /* Set command */
    memset(cmd, 0, sizeof(zbc_sg_cmd_t));
    cmd->code = cmd_code;
    cmd->cdb_sz = zbc_sg_cmd_list[cmd_code].cdb_length;
    zbc_assert(cmd->cdb_sz <= ZBC_SG_CDB_MAX_LENGTH);
    cmd->cdb_opcode = zbc_sg_cmd_list[cmd_code].cdb_opcode;
    cmd->cdb_sa = zbc_sg_cmd_list[cmd_code].cdb_sa;

    /* Set output buffer */
    if ( out_buf ) {

        /* Set specified buffer */
        if ( ! out_bufsz ) {
            zbc_error("Invalid 0 output buffer size\n");
            ret = -EINVAL;
            goto out;
        }

        cmd->out_buf = out_buf;
        cmd->out_bufsz = out_bufsz;

    } else if ( out_bufsz ) {

        /* Allocate a buffer */
        cmd->out_bufsz = out_bufsz;
        ret = posix_memalign((void **) &cmd->out_buf, sysconf(_SC_PAGESIZE), cmd->out_bufsz);
        if ( ret != 0 ) {
            zbc_error("No memory for output buffer (%zu B)\n",
                      out_bufsz);
            ret = -ENOMEM;
            goto out;
        }
        memset(cmd->out_buf, 0, out_bufsz);
        cmd->out_buf_needfree = 1;

    }

    /* OK: setup SGIO header */
    memset(&cmd->io_hdr, 0, sizeof(sg_io_hdr_t));

    cmd->io_hdr.interface_id    = 'S';
    cmd->io_hdr.timeout         = 20000;
    cmd->io_hdr.flags           = SG_FLAG_DIRECT_IO;

    cmd->io_hdr.cmd_len         = cmd->cdb_sz;
    cmd->io_hdr.cmdp            = &cmd->cdb[0];

    cmd->io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    cmd->io_hdr.dxfer_len       = cmd->out_bufsz;
    cmd->io_hdr.dxferp          = cmd->out_buf;

    cmd->io_hdr.mx_sb_len       = ZBC_SG_SENSE_MAX_LENGTH;
    cmd->io_hdr.sbp             = cmd->sense_buf;

out:

    if ( ret != 0 ) {
        zbc_sg_cmd_destroy(cmd);
    }

    return( ret );

}

/**
 * Execute a command.
 */
int
zbc_sg_cmd_exec(zbc_device_t *dev,
                zbc_sg_cmd_t *cmd)
{
    char msg[512];
    int n, i = 0, j, ret;

    if ( zbc_log_level >= ZBC_LOG_DEBUG ) {

        zbc_debug("*****************************************\n");

        zbc_debug("* Sending cmd 0x%02x:0x%02x (%s) to device %s:\n",
                  cmd->cdb_opcode,
                  cmd->cdb_sa,
                  zbc_sg_cmd_name(cmd),
                  dev->zbd_filename);
        
        zbc_debug("* +==================================\n");
        zbc_debug("* |Byte |   0  |  1   |  2   |  3   |\n");
        zbc_debug("* |=====+======+======+======+======+\n");
        while( i < cmd->cdb_sz ) {

            n = 0;
            for(j = 0; j < 4; j++) {
                if ( i < cmd->cdb_sz ) {
                    n += sprintf(msg + n, " 0x%02x |",
                                 (int)cmd->cdb[i]);
                } else {
                    n += sprintf(msg + n, "      |");
                }
                i++;
            }

            zbc_debug("* | %3d |%s\n", i, msg);
            if ( i < (cmd->cdb_sz - 4) ) {
                zbc_debug("* |=====+======+======+======+======+\n");
            } else {
                zbc_debug("* +==================================\n");
            }
        }

        zbc_debug("*****************************************\n");

    }

    /* Send the SG_IO command */
    ret = ioctl(dev->zbd_fd, SG_IO, &cmd->io_hdr);
    if( ret != 0 ) {
        zbc_error("%s: SG_IO ioctl failed %d (%s)",
                  dev->zbd_filename,
                  errno,
                  strerror(errno));
        ret = -errno;
        goto out;
    }

    /* Check status */
    if ( cmd->io_hdr.status != 0 ) {

        int sense_buff_idx = 0;

        zbc_error("%s: Command %s failed with status = 0x%02x\n",
                  dev->zbd_filename,
                  zbc_sg_cmd_name(cmd),
                  (int)cmd->io_hdr.status);
        zbc_error("Sense buffer:\n");
        while( sense_buff_idx < cmd->io_hdr.sb_len_wr ) {
            zbc_error("[%02u]: 0x%02x 0x%02x 0x%02x 0x%02x\n",
                      sense_buff_idx,
                      cmd->sense_buf[sense_buff_idx],
                      cmd->sense_buf[sense_buff_idx + 1],
                      cmd->sense_buf[sense_buff_idx + 2],
                      cmd->sense_buf[sense_buff_idx + 3]);
            sense_buff_idx += 4;
        }

        ret = -EIO;

        goto out;

    }

    if ( cmd->io_hdr.host_status != 0 ) {
        zbc_error("%s: Command %s failed with host adapter status status = 0x%04x\n",
                  dev->zbd_filename,
                  zbc_sg_cmd_name(cmd),
                  (int)cmd->io_hdr.host_status);
        ret = -EIO;
        goto out;
    }

    if ( cmd->io_hdr.driver_status != 0 ) {
        zbc_error("%s: Command %s failed with driver status status = 0x%04x\n",
                  dev->zbd_filename,
                  zbc_sg_cmd_name(cmd),
                  (int)cmd->io_hdr.driver_status);
        ret = -EIO;
        goto out;
    }

    if ( cmd->io_hdr.resid ) {
        zbc_debug("%s: Transfer missing %d B of data\n",
                  dev->zbd_filename,
                  cmd->io_hdr.resid);
        cmd->out_bufsz -= cmd->io_hdr.resid;
    }

    zbc_debug("%s: Command %s executed in %u ms\n",
              dev->zbd_filename,
              zbc_sg_cmd_name(cmd),
              cmd->io_hdr.duration);

out:

    return( ret );

}

/**
 * Set bytes in a command cdb.
 */
void
zbc_sg_cmd_set_bytes(uint8_t *cmd,
                     void *buf,
                     int bytes)
{
    uint8_t *v = (uint8_t *) buf;
    int i;

    for(i = 0; i < bytes; i++) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        /* The least significant byte stored is first */
        cmd[bytes - i - 1] = v[i];
#else
        /* The most significant byte being is stored first */
        cmd[i] = v[i];
#endif
    }

    return;

}

/**
 * Get bytes from a command output buffer.
 */
void
zbc_sg_cmd_get_bytes(uint8_t *val,
                     union converter *conv,
                     int bytes)
{
    uint8_t *v = (uint8_t *) val;
    int i;

    memset(conv, 0, sizeof(union converter));

    for(i = 0; i < bytes; i++) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        conv->val_buf[bytes - i - 1] = v[i];
#else
        conv->val_buf[i] = v[i];
#endif
    }

    return;

}
