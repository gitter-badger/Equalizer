#!/usr/bin/python

from common import *

import subprocess
import shlex
import re
import startServers

def findInactiveXServers():
   
    xServerList = []
    gpuList = subprocess.Popen(["gpu_sd_list"], stdout=subprocess.PIPE).communicate()[0]
    for i in range( 1, numberOfServers + 1 ):
       nodeNumberStr = str(i).zfill(2)
       numberOfGPUs = len([(a.start(), a.end()) for a in list(re.finditer('node' + nodeNumberStr, gpuList))])
       if numberOfGPUs < 3:
          xServerList.append( 'node' + nodeNumberStr )
       
    return xServerList

def startXServers( servers ):
   xCommand = 'sudo X -novtswitch -sharevts -ac 2>&1 | logger -t Xorg) &'
   for server in servers:
      sshCommand = 'ssh ' + server
      args = shlex.split(sshCommand + ' ' + xCommand)
      subprocess.Popen(args, stdout=subprocess.PIPE, shell=True)

def main():
    startServers.startServers(1, numberOfServers, 'x-y-z')
    servers = findInactiveXServers()
    # startXServers( servers )
    print servers
    startServers.stopServers()

if __name__ == "__main__":
    main()