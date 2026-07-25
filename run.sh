idf.py flash -p /dev/ttyACM0 -b 115200 monitor
idf.py openocd --openocd-commands "-f board/esp32s3-builtin.cfg"
idf.py gdb
