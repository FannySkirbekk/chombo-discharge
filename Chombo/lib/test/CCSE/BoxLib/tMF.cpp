#ifdef CH_LANG_CC
/*
 *      _______              __
 *     / ___/ /  ___  __ _  / /  ___
 *    / /__/ _ \/ _ \/  V \/ _ \/ _ \
 *    \___/_//_/\___/_/_/_/_.__/\___/
 *    Please refer to Copyright.txt, in Chombo's root directory.
 */
#endif

//
// A test program for MultiFab.
//

#include <winstd.H>

#include <Utility.H>
#include <MultiFab.H>

int
main (int argc, char** argv)
{
    BoxLib::Initialize(argc, argv);

    Box bx1(IntVect::TheZeroVector(),IntVect::TheUnitVector());
    Box bx2 = bx1;
    bx2.shift(0,2);

    BoxArray ba(2);
    ba.set(0, bx1);
    ba.set(1, bx2);

    MultiFab junk(ba,2,1);

    BoxLib::Finalize();

    return 0;
}
