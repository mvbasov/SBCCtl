#!/bin/bash
cd bin
zip SBCCtl_$(git describe --tags --abbrev=0 HEAD)_$(git rev-parse --short HEAD).zip *.bin flash.sh
cd -
