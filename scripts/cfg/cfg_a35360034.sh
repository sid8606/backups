#!/bin/bash
vmcp "att pcif 12D3 to *"
vmcp "att pcif 12D4 to *"
vmcp "att pcif 12D5 to *"
vmcp "att pcif 12D6 to *"

vmcp "att pcif 327 to *"
vmcp "att pcif 328 to *" 

vmcp "att E303-E305 to *"

cio_ignore -r E303-E305
chzdev -e E303-E305

nmcli device set eno4819 managed off
nmcli device set eno4820 managed off
nmcli device set eno4821 managed off
nmcli device set eno4822 managed off

nmcli device set ence303 managed off

ip addr add 10.10.3.54/16 dev eno4819 
ip addr add 10.10.4.55/16 dev eno4820
ip addr add 10.10.5.56/16 dev eno4821
ip addr add 10.10.6.57/16 dev eno4822

ip addr add 10.10.1.20/16 dev ence303

systemctl stop firewalld
systemctl disable firewalld

smcr ueid add SID
smc_pnet -D 12d3:00:00.0 -a NET10
smc_pnet -D 12d4:00:00.0 -a NET10
smc_pnet -D 12d5:00:00.0 -a NET10
smc_pnet -D 12d6:00:00.0 -a NET10
