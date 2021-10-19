#!/vendor/bin/sh
cat /mnt/vendor/persist/engineermode/tri_state_hall_data | tr ';' ',' > /sys/devices/platform/soc/soc:tri_state_key/hall_data_calib
