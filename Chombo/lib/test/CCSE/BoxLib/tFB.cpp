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
// A test program for FillBoundary().
//

#include <Utility.H>
#include <MultiFab.H>

static
void
DumpFirstFab (const MultiFab& mf)
{
    CH_ASSERT(mf.size() > 1);

    if (ParallelDescriptor::IOProcessor())
    {
        CH_ASSERT(mf.DistributionMap()[0] == ParallelDescriptor::MyProc());

        std::cout << mf[0] << std::endl;
    }
}

static
void
DoIt (MultiFab& mf)
{
    CH_ASSERT(mf.nComp() >= 2);

    mf.setVal(0);
    //
    // Do loop just filling first component.
    //
    for (int i = 0; i < 10; i++)
    {
        for (MFIter mfi(mf); mfi.isValid(); ++mfi)
        {
            mf[mfi].setVal(mfi.index()+1,0);
        }

        mf.FillBoundary(0,1);

        DumpFirstFab(mf);
    }
    //
    // Do loop just filling second component.
    //
    for (int i = 0; i < 10; i++)
    {
        for (MFIter mfi(mf); mfi.isValid(); ++mfi)
        {
            mf[mfi].setVal(mfi.index()+10,1);
        }

        mf.FillBoundary(1,1);

        DumpFirstFab(mf);
    }
    //
    // Do loop filling all components.
    //
    for (int i = 0; i < 10; i++)
    {
        for (MFIter mfi(mf); mfi.isValid(); ++mfi)
        {
            mf[mfi].setVal(mfi.index()+100);
        }

        mf.FillBoundary();

        DumpFirstFab(mf);
    }
}

int
main (int argc, char** argv)
{
    BoxLib::Initialize(argc, argv);

    BoxList bl;

    Box bx1(IntVect::TheZeroVector(),IntVect::TheUnitVector());
    Box bx2 = bx1;
    bx2.shift(0,2);

    bl.push_back(bx1); bl.push_back(bx2);

    BoxArray ba(bl);

    MultiFab junk(ba,2,1), junky(ba,2,1);

    DoIt(junk); DoIt(junky); DoIt(junk); DoIt(junky);

    BoxLib::Finalize();

    return 0;
}
