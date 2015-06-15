#!/bin/bash

if [ $# -ne 1 ]; then
  echo "[usage] $0 <target_device>"
  echo "    target_device          : device file. e.g. /dev/sg3"
  exit 1
fi

testbase=${0##*/}
testname=${testbase%.*}

device=${1}
nr_test=${testname}

expected_sk="Aborted-command"
expected_asc="Insufficient-zone-resources"


# Delete old log
rm -Rf ${nr_test}.log
rm -Rf ${nr_test}_zone_info.log

# Test print
echo "[TEST][${nr_test}][OPEN_ZONE][ALL][INSUFFICIENT_ZONE_RESOURCES] start"


######################## Get drive info ######################## 
sudo zbc_test_print_devinfo ${device} >> ${nr_test}.log 2>&1

# Get zbc specific info
_IFS="${IFS}"
IFS=','

max_open_line=`cat ${nr_test}.log | grep -F "[MAX_NUM_OF_OPEN_SWRZ]"`
set -- ${max_open_line}
max_open=${2}

max_lba_line=`cat ${nr_test}.log | grep -F "[MAX_LBA]"`
set -- ${max_lba_line}
max_lba=${2}

unrestricted_read_line=`cat ${nr_test}.log | grep -F "[URSWRZ]"`
set -- ${unrestricted_read_line}
unrestricted_read=${2}

IFS="$_IFS"


######################## Get zone info ######################## 
# Get zone info
sudo zbc_test_report_zones ${device} > ${nr_test}_zone_info.log 2>&1

######################## Determin target zone ######################## 
_IFS="${IFS}"
IFS=','

declare -i count=0
cat ${nr_test}_zone_info.log | grep -F "[ZONE_INFO]" |\
while read _line; do
    set -- ${_line}

    if [ ${count} -eq $(( ${max_open} + 1 )) ]; then
        break
    fi

    if [ ${3} = "0x2" ]; then
        sudo zbc_test_write_zone -v ${device} ${5} >> ${nr_test}.log 2>&1
        sudo zbc_test_close_zone -v ${device} ${5} >> ${nr_test}.log 2>&1
        count=${count}+1
    fi

done

IFS="$_IFS"

######################## Opening implicitly by writing ######################## 

######################## Start Testing ######################## 
# Execution command
sudo zbc_test_open_zone -v ${device} -1 >> ${nr_test}.log 2>&1

# Get SenseKey, ASC/ASCQ
_IFS="${IFS}"
IFS=','

sk_line=`cat ${nr_test}.log | grep -F "[SENSE_KEY]"`
set -- ${sk_line}
sk=${2}

asc_line=`cat ${nr_test}.log | grep -F "[ASC_ASCQ]"`
set -- ${asc_line}
asc=${2}

IFS="$_IFS"

######################## Check result ######################## 
# Check result
if [ ${sk} = ${expected_sk} -a ${asc} = ${expected_asc} ]; then
    echo "[TEST][${nr_test}],Passed"
else
    echo "[TEST][${nr_test}],Failed"
    echo "[TEST][${nr_test}][SENSE_KEY],${sk} instead of ${expected_sk}"
    echo "[TEST][${nr_test}][ASC_ASCQ],${asc} instead of ${expected_asc}"
fi

######################## Reset all zones ######################## 
sudo zbc_test_reset_write_ptr ${device} -1
sudo zbc_test_close_zone ${device} -1
rm -Rf ${nr_test}_zone_info.log
