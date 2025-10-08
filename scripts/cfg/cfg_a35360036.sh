#!/bin/bash
vmcp "att pcif 1293 to *"
vmcp "att pcif 1294 to *"
vmcp "att pcif 1295 to *"
vmcp "att pcif 1296 to *"

vmcp "att pcif 325 to *"
vmcp "att pcif 326 to *" 

vmcp "att E400-E402 to *"
vmcp "att E403-E405 to *"

cio_ignore -r E400-E403
cio_ignore -r E403-E405

chzdev -e E400-E403
chzdev -e E403-E405

nmcli device set eno4755  managed off
nmcli device set eno4756  managed off
nmcli device set eno4757  managed off
nmcli device set eno4758  managed off

nmcli device set ence400 managed off
nmcli device set ence403 managed off

ip addr add 10.10.11.14/16 dev eno4755
ip addr add 10.10.12.15/16 dev eno4756
ip addr add 10.10.13.16/16 dev eno4757
ip addr add 10.10.14.17/16 dev eno4758

ip addr add 10.10.4.10/16 dev ence400
ip addr add 10.10.5.20/16 dev ence403

systemctl stop firewalld
systemctl disable firewalld

smcr ueid add SID
