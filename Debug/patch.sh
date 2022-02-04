#!/bin/sh
for i in *.mk *.d makefile; do
	sed -i 's/C:\/ti\/ccs810/\/home\/test\/ccs8/g' "$i"
	#sed -i 's/C:\/ti/\/home\/test\/ti/g' "$i"
	sed -i 's/C:\/ti/\/home\/test\/ti/g' "$i"
	sed -i '/cmd\.exe/d' "$i"
done
