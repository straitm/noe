#!/bin/bash

doit()
{
  type=$1

  echo '#include "noe.fcl"'
  if [ "$type" != notracks ]; then
    echo '# for geometry for tracks'
    echo '#include "services.fcl"'
  fi
  echo

  printf 'process_name: noe\n\n'

  echo 'services:'
  echo '{'
  if [ "$type" != notracks ]; then
    echo '  # For tracks'
    echo '  Geometry: @local::standard_geo'
    echo '  Detector: @local::standard_detector'
  fi
  echo '  # Suppress "Begin processing the nth record" messages'
  echo '  message: { destinations: { debugmsg:{ threshold: "WARNING"} } }'
  printf '}\n\n'

  if [ "$type" != default ]; then
     echo "UUDDLRLRBAS: @local::standard_noe"
     if [ $type != "notracks" ]; then
       label=kalmantrackmerge
     fi
     printf 'UUDDLRLRBAS.track_label: "'$label'"\n\n'
  fi

  echo "physics:"
  echo "{"
  echo "  producers:"
  echo "  {"
  name=UUDDLRLRBAS
  if [ "$type" == default ]; then
    name=standard_noe
  fi
  echo "    noe: @local::$name"
  echo "  }"
  echo
  echo "  reco: [ noe ]"
  echo "}"
}

doit default  > noejob.fcl
doit notracks > noejob_notracks.fcl
doit kalman   > noejob_kalman.fcl
