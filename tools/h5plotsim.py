#!/usr/bin/env python
"""Plot simulation results
"""

from __future__ import print_function

import sys

from matplotlib.pylab import *

from h5py import File

fname, _junk, gname = sys.argv[1].partition(':')

data = File(fname)
grp  = data[gname or '/']

simtype = grp.attrs['sim_type']

print('sim_type', simtype)

def show_vector(grp, vector='state'):
    'Show envelope size and angle as a function of s position'
    pos   = grp['pos']
    state = grp[vector]

    subplot(2,1,1)
    plot(pos, state[:,0], '-b',
         pos, state[:,2], '-r',
         pos, state[:,4], '-g')
    xlabel('s')
    ylabel('size')
    legend(['x','y','z'])

    subplot(2,1,2)
    plot(pos, state[:,1], '-b',
         pos, state[:,3], '-r',
         pos, state[:,5], '-g')
    xlabel('s')
    ylabel('angle')

def show_generic(grp):
    print("Unknown sim_type")

showsim = {
  'Vector': show_vector,
  'MomentMatrix2': lambda x:show_vector(x,'moment0'),
}

showsim.get(simtype)(grp)

show()
