#!/bin/bash
count=12
pgname='fswebcam'$count
`./$pgname -r 1280x720 -S 15 --jpeg 99 -p MJPEG --shadow --title "Eating" --subtitle "By Charmyin" --info "Author: Charmyin" -q --save /mnt/toshibausb/test`
echo $pgname
