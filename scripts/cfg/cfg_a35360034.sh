#!/bin/bash
vmcp "att pcif 12D3 to *"
vmcp "att pcif 12D4 to *"
vmcp "att pcif 12D5 to *"
vmcp "att pcif 12D6 to *"

vmcp "att pcif 327 to *"
vmcp "att pcif 328 to *" 

vmcp "att E406-E408 to *"
vmcp "att E409-E40B to *"

cio_ignore -r E406-E408
cio_ignore -r E409-E40B

chzdev -e E409-E40B
chzdev -e E406-E408

nmcli device set eno4819 managed off
nmcli device set eno4820 managed off
nmcli device set eno4821 managed off
nmcli device set eno4822 managed off

nmcli device set ence406 managed off
nmcli device set ence409 managed off

ip addr add 10.10.3.54/16 dev eno4819 
ip addr add 10.10.4.55/16 dev eno4820
ip addr add 10.10.5.56/16 dev eno4821
ip addr add 10.10.6.57/16 dev eno4822

ip addr add 10.10.1.30/16 dev ence406
ip addr add 10.10.2.40/16 dev ence409

systemctl stop firewalld
systemctl disable firewalld

smcr ueid add SID
