#!/bin/bash
cd bin
last_tag=$(git describe --tags --abbrev=0 HEAD)
commit_count=$(git rev-list --count ${last_tag}..HEAD)
current_commit=$(git rev-parse --short HEAD)
if [[ ${commit_count} -eq "0" ]]; then
  bin_version="${last_tag}-${current_commit}"
else
  bin_version="${last_tag}-${commit_count}-${current_commit}"
fi
zip SBCCtl_${bin_version}.zip *.bin flash.sh
cd -
