

connect -url tcp:127.0.0.1:3121
targets -set -nocase -filter {name =~"APU*"}
rst -system
after 3000
fpga -file cva6_fpga.runs/impl_1/cva6_zybo_z7_20.bit
targets -set -nocase -filter {name =~"APU*"}
source scripts/ps7_init.tcl
ps7_init
ps7_post_config

