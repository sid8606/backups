#!/bin/bash
vmcp "att pcif 1293 to *"
vmcp "att pcif 1294 to *"
vmcp "att pcif 1295 to *"
vmcp "att pcif 1296 to *"

vmcp "att pcif 325 to *"
vmcp "att pcif 326 to *" 

vmcp "att E300-E302 to *"

chzdev -e E300-E302

nmcli device set eno4755  managed off
nmcli device set eno4756  managed off
nmcli device set eno4757  managed off
nmcli device set eno4758  managed off

nmcli device set ence300 managed off

ip addr add 10.10.11.14/16 dev eno4755
ip addr add 10.10.12.15/16 dev eno4756
ip addr add 10.10.13.16/16 dev eno4757
ip addr add 10.10.14.17/16 dev eno4758

ip addr add 10.10.4.10/16 dev ence300

systemctl stop firewalld
systemctl disable firewalld

smcr ueid add SID
smc_pnet -D 1293:00:00.0 -a NET10
smc_pnet -D 1294:00:00.0 -a NET10
smc_pnet -D 1295:00:00.0 -a NET10
smc_pnet -D 1296:00:00.0 -a NET10
