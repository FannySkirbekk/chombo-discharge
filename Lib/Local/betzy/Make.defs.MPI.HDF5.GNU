# Copy this file to:
#
#    Chombo/lib/mk/Make.defs.local
#
# before modifying it.
#
# Use this file to set Chombo makefile variables for the local
# installation.  To set variables for just a single computer
# create a file named:
#
#    Chombo/lib/mk/local/Make.defs.<hostname>
#
# where <hostname> is the value of `uname -n` (or `hostname`, if uname doesn't work)

# The default values for the main variables here are in:
#
#    Chombo/lib/mk/Make.defs.defaults

# This file is include'd from Make.defs after:
#    Make.defs.defaults
# and before:
#    Make.defs.<hostname> Make.defs.config Make.rules

makefiles+=Make.defs.local

################################################################

# Chombo configuration variables that affect filenames:

#  DIM          :(2,3) number of dimensions in PDEs
#  DEBUG        :(TRUE,FALSE) compile with symbol table or not
#  OPT          :(TRUE,HIGH,FALSE) optimize, high optimize, or not
#  PRECISION    :(FLOAT,DOUBLE) size of floating point variables
#  PROFILE      :(TRUE,FALSE) compile for performance profiling or not
#  CXX          : command to compile/link C++ code
#  FC           : command to compile Fortran code
#  MPI          :(TRUE,FALSE) compile for parallel if TRUE, else serial
#  OPENMPCC     : TRUE for threading with OpenMP, FALSE without OpenMP
#  MPICXX       : command to compile/link C++ code in parallel
#  ROSE         : (TRUE,FALSE) preprocess with Rose-based translator
#  GPU          : (TRUE,FALSE) compile CUDA kernels
#  OBJMODEL     : special config for non-default compiler mode
#  XTRACONFIG   : user-defined special config

# Variables for optional features that don't affect filenames:

#  CXXSTD          : Chombo default is C++11.  Set to 14 for C++14, etc.  This
#                    should not be changed unless you want to experiment with
#                    different C++ standards.  Only the default is supported.
#  USE_64          : if TRUE, use 64bit pointers on systems where 32bits is the default
#  USE_COMPLEX     : if TRUE, enable the 'Complex' type
#                    (default is TRUE, disable only if compiler doesn't allow it)
#  USE_EB          : if TRUE, build Chombo Embedded Boundary code
#  USE_CCSE        : if TRUE, build CCSE mglib and supporting code into one lib
#  USE_HDF         : if TRUE, use the HDF5 library
#   HDFINCFLAGS    : cpp options (-I*) for HDF5 include files
#   HDFLIBFLAGS    : link options (-L* -l*) for HDF5 library files
#   HDFMPIINCFLAGS : cpp options (-I*) for parallel HDF5 include files
#   HDFMPILIBFLAGS : link options (-L* -l*) for parallel HDF5 library files
#  USE_MF          : if TRUE, build Chombo MultiFluid code (requires USE_EB=TRUE)
#  USE_MT          : if TRUE, enable Chombo memory tracking
#  USE_SETVAL      : (TRUE,FALSE) use setVal to initialize all BaseFab<Real>

# These variables are system-dependent but usually dont have to be changed:

#  CH_AR       : command to add object files to a library file
#  CH_CPP      : command to run the C preprocessor on Fortran files
#  DOXYGEN  : command to run the 'doyxgen' program
#  LD       : command to run the linker (default: use $CXX or $MPICXX, as appropriate)
#  PERL     : command to run perl
#  RANLIB   : command to post-process a library archive for random access

# Compiler variables.  The 'Make.rules' file chooses whether to use
# the 'opt' flags or the 'dbg' flags.  The 'prof' flags are added if
# PROFILE=TRUE.
#
#  cppdbgflags : debug options for the C-preprocessor (both C++ and Fortran)
#  cppoptflags : optimization options for the C-preprocessor (")
#  cxxcppflags : C-preprocessor flags for C++ compiles only
#  cxxdbgflags : debug options for the C++ and MPIC++ compilers
#  cxxoptflags : optimization options for the C++ and MPIC++ compilers
#  cxxprofflags: profiling options for the C++ and MPIC++ compilers
#  fcppflags   : C-preprocessor flags for Fortran compiles only
#  fdbgflags   : debug options for the Fortran compiler
#  foptflags   : optimization options for the Fortran compiler
#  fprofflags  : profiling options for the Fortran compiler
#  lddbgflags  : debug options for the linker
#  ldoptflags  : optimization options for the linker
#  ldprofflags : profiling options for the linker
#  flibflags   : options for the linker to specify the Fortran libraries
#                (this usually is needed only when mixing C++ and
#                 Fortran compilers from different vendors)
#  syslibflags : extra linker options to get libraries needed on this system
#
# Notes:
# 1) The linker command always uses the CXX options in addition to its own variables,
#    so options that are common to both do not need to be repeated in the ld*flags vars
#    (e.g. -g for debugging, or -pg for profiling)
# 2) The compiler options can be set on the command line using the
#    variables: CPPFLAGS CXXFLAGS FFLAGS LDFLAGS.
#    These variables supercede any settings in the makefiles.
# 3) The GNUmakefile in "Chombo/lib" will check these variables and complain if
#    they have invalid values.  On systems that don't have MPI installed, the
#    various MPI variables should not be set (or should be set to empty strings)
#    to prevent the checker from complaining.

#begin  -- dont change this line

## Override the default values here

## Configuration variables
DIM            = 3
DEBUG          = FALSE
OPT            = HIGH
#PRECISION     =
#PROFILE       =
CXX            = g++
FC             = gfortran
MPI            = TRUE
## Note: don't set the MPICXX variable if you don't have MPI installed
MPICXX         = mpic++
#OBJMODEL      =
#XTRACONFIG    =
## Optional features
CXXSTD         = 14
#USE_64        =
#USE_COMPLEX   =
USE_EB         = TRUE
#USE_CCSE      =
USE_HDF        = TRUE
HDFINCFLAGS	= -I/cluster/software/HDF5/1.14.0-gompi-2023a/include  -DH5_USE_110_API
HDFLIBFLAGS	= -L/cluster/software/HDF5/1.14.0-gompi-2023a/lib -lhdf5 -lz  -DH5_USE_110_API
## Note: don't set the HDFMPI* variables if you don't have parallel HDF installed
HDFMPIINCFLAGS	= -I/cluster/software/HDF5/1.14.0-gompi-2023a/include -DH5_USE_110_API
HDFMPILIBFLAGS	= -L/cluster/software/HDF5/1.14.0-gompi-2023a/lib -lhdf5 -lz -DH5_USE_110_API 
USE_MF         = TRUE
#USE_MT        =
#USE_SETVAL    =
#CH_AR         =
#CH_CPP        =
#DOXYGEN       =
#LD            =
#PERL          =
#RANLIB        =
#ROSE          =
#ROSECC        =
#ROSEINCFLAGS  =
#ROSELIBFLAGS  =
## Note: don't set the CUDA variables if you are not using a CUDA GPU
#GPU           =
#NVCC          =
#NVCCFLAGS     =
#PTXASFLAGS    =
#CUBIN         =
#cppdbgflags   =
#cppoptflags   =
#cxxcppflags   =
#cxxdbgflags   =
cxxoptflags    = -O3
#cxxprofflags  =
#fcppflags     =
#fdbgflags     =
foptflags      = -O3
#fprofflags    =
#flibflags     =
#lddbgflags    =
#ldoptflags    =
#ldprofflags   =
syslibflags    = -lopenblas

#end  -- dont change this line
