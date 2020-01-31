//
// $Id: AmrLevel.cpp,v 1.1 2007-05-24 18:12:02 tdsternberg Exp $
//
#include <winstd.H>

#include <cstdio>
#include <cstring>
#include <sstream>

#include <AmrLevel.H>
#include <Derive.H>
#include <ParallelDescriptor.H>
#include <Utility.H>
#include <ParmParse.H>
#include <Profiler.H>

DescriptorList AmrLevel::desc_lst;
DeriveList     AmrLevel::derive_lst;
SlabStatList   AmrLevel::slabstat_lst;

void
AmrLevel::postCoarseTimeStep (Real time)
{}

int
AmrLevel::Level () const
{
    return level;
}

const BoxArray&
AmrLevel::boxArray () const
{
    return grids;
}

int
AmrLevel::numGrids () const
{
    return grids.size();
}

const Array<RealBox>&
AmrLevel::gridLocations () const
{
    return grid_loc;
}

const Box&
AmrLevel::Domain () const
{
    return geom.Domain();
}

int
AmrLevel::nStep () const
{
    return parent->levelSteps(level);
}

const Geometry&
AmrLevel::Geom () const
{
    return geom;
}

void
AmrLevel::setPhysBoundaryValues (int state_indx,
                                 int comp,
                                 int ncomp,
                                 int do_new)
{
    state[state_indx].FillBoundary(geom.CellSize(),
                                   geom.ProbDomain(),
                                   comp,
                                   ncomp,
                                   do_new);
}

StateData&
AmrLevel::get_state_data (int state_indx)
{
    return state[state_indx];
}

MultiFab&
AmrLevel::get_old_data (int state_indx)
{
    return state[state_indx].oldData();
}

const MultiFab&
AmrLevel::get_old_data (int state_indx) const
{
    return state[state_indx].oldData();
}

MultiFab&
AmrLevel::get_new_data (int state_indx)
{
    return state[state_indx].newData();
}

const MultiFab&
AmrLevel::get_new_data (int state_indx) const
{
    return state[state_indx].newData();
}

const DescriptorList&
AmrLevel::get_desc_lst ()
{
    return desc_lst;
}

SlabStatList&
AmrLevel::get_slabstat_lst ()
{
    return slabstat_lst;
}

void
AmrLevel::set_preferred_boundary_values (MultiFab& S,
                                         int       state_index,
                                         int       scomp,
                                         int       dcomp,
                                         int       ncomp,
                                         Real      time) const
{}

DeriveList&
AmrLevel::get_derive_lst ()
{
    return derive_lst;
}

void
AmrLevel::manual_tags_placement (TagBoxArray&    tags,
                                 Array<IntVect>& bf_lev)
{}

AmrLevel::AmrLevel ()
{
   parent = 0;
   level = -1;
}

AmrLevel::AmrLevel (Amr&            papa,
                    int             lev,
                    const Geometry& level_geom,
                    const BoxArray& ba,
                    Real            time)
    :
    geom(level_geom),
    grids(ba)
{
    level  = lev;
    parent = &papa;

    fine_ratio = IntVect::TheUnitVector(); fine_ratio.scale(-1);
    crse_ratio = IntVect::TheUnitVector(); crse_ratio.scale(-1);

    if (level > 0)
    {
        crse_ratio = parent->refRatio(level-1);
    }
    if (level < parent->maxLevel())
    {
        fine_ratio = parent->refRatio(level);
    }

    state.resize(desc_lst.size());

    for (int i = 0; i < state.size(); i++)
    {
        state[i].define(geom.Domain(),
                        grids,
                        desc_lst[i],
                        time,
                        parent->dtLevel(lev));
    }

    finishConstructor();
}

void
AmrLevel::restart (Amr&          papa,
                   std::istream& is,
		   bool          bReadSpecial)
{
    parent = &papa;

    is >> level;
    is >> geom;

    fine_ratio = IntVect::TheUnitVector(); fine_ratio.scale(-1);
    crse_ratio = IntVect::TheUnitVector(); crse_ratio.scale(-1);

    if (level > 0)
    {
        crse_ratio = parent->refRatio(level-1);
    }
    if (level < parent->maxLevel())
    {
        fine_ratio = parent->refRatio(level);
    }

    if (bReadSpecial)
    {
        BoxLib::readBoxArray(grids, is, bReadSpecial);
    }
    else
    {
        grids.readFrom(is);
    }

    int nstate;
    is >> nstate;
    int ndesc = desc_lst.size();
    BL_ASSERT(nstate == ndesc);

    state.resize(ndesc);
    for (int i = 0; i < ndesc; i++)
    {
        state[i].restart(is, desc_lst[i], papa.theRestartFile(), bReadSpecial);
    }

    finishConstructor();
}

void
AmrLevel::finishConstructor ()
{
    //
    // Set physical locations of grids.
    //
    grid_loc.resize(grids.size());

    for (int i = 0; i < grid_loc.size(); i++)
    {
        grid_loc[i] = RealBox(grids[i],geom.CellSize(),geom.ProbLo());
    }
}

void
AmrLevel::setTimeLevel (Real time,
                        Real dt_old,
                        Real dt_new)
{
    for (int k = 0; k < desc_lst.size(); k++)
    {
        state[k].setTimeLevel(time,dt_old,dt_new);
    }
}

bool
AmrLevel::isStateVariable (const std::string& name,
                           int&           typ,
                           int&            n)
{
    for (typ = 0; typ < desc_lst.size(); typ++)
    {
        const StateDescriptor& desc = desc_lst[typ];

        for (n = 0; n < desc.nComp(); n++)
        {
            if (desc.name(n) == name)
                return true;
        }
    }
    return false;
}

long
AmrLevel::countCells () const
{
    long cnt = 0;
    for (int i = 0; i < grids.size(); i++)
    {
        cnt += grids[i].numPts();
    }
    return cnt;
}

void
AmrLevel::checkPoint (const std::string& dir,
                      std::ostream&  os,
                      VisMF::How     how)
{
    int ndesc = desc_lst.size(), i;
    //
    // Build directory to hold the MultiFabs in the StateData at this level.
    // The directory is relative the the directory containing the Header file.
    //
    char buf[64];
    sprintf(buf, "Level_%d", level);
    std::string Level = buf;
    //
    // Now for the full pathname of that directory.
    //
    std::string FullPath = dir;
    if (!FullPath.empty() && FullPath[FullPath.length()-1] != '/')
    {
        FullPath += '/';
    }
    FullPath += Level;
    //
    // Only the I/O processor makes the directory if it doesn't already exist.
    //
    if (ParallelDescriptor::IOProcessor())
        if (!BoxLib::UtilCreateDirectory(FullPath, 0755))
            BoxLib::CreateDirectoryFailed(FullPath);
    //
    // Force other processors to wait till directory is built.
    //
    ParallelDescriptor::Barrier();

    if (ParallelDescriptor::IOProcessor())
    {
        os << level << '\n' << geom  << '\n';
        grids.writeOn(os);
        os << ndesc << '\n';
    }
    //
    // Output state data.
    //
    for (i = 0; i < ndesc; i++)
    {
        //
        // Now build the full relative pathname of the StateData.
        // The name is relative to the Header file containing this name.
        // It's the name that gets written into the Header.
        //
        // There is only one MultiFab written out at each level in HyperCLaw.
        //
        std::string PathNameInHeader = Level;
        sprintf(buf, "/SD_%d", i);
        PathNameInHeader += buf;
        std::string FullPathName = FullPath;
        FullPathName += buf;
        state[i].checkPoint(PathNameInHeader, FullPathName, os, how);
    }
}

AmrLevel::~AmrLevel ()
{
    parent = 0;
}

void
AmrLevel::allocOldData ()
{
    for (int i = 0; i < desc_lst.size(); i++)
    {
        state[i].allocOldData();
    }
}

void
AmrLevel::removeOldData ()
{
    for (int i = 0; i < desc_lst.size(); i++)
    {
        state[i].removeOldData();
    }
}

void
AmrLevel::reset ()
{
    for (int i = 0; i < desc_lst.size(); i++)
    {
        state[i].reset();
    }
}

MultiFab&
AmrLevel::get_data (int  state_indx,
                    Real time)
{
    const Real old_time = state[state_indx].prevTime();
    const Real new_time = state[state_indx].curTime();
    const Real eps = 0.001*(new_time - old_time);

    if (time > old_time-eps && time < old_time+eps)
    {
        return get_old_data(state_indx);
    }
    else if (time > new_time-eps && time < new_time+eps)
    {
        return get_new_data(state_indx);
    }
    else
    {
        BoxLib::Error("get_data: invalid time");
        static MultiFab bogus;
        return bogus;
    }
}

void
AmrLevel::setPhysBoundaryValues (int  state_indx,
                                 int  comp,
                                 int  ncomp,
                                 Real time)
{
    const Real old_time = state[state_indx].prevTime();
    const Real new_time = state[state_indx].curTime();
    const Real eps = 0.001*(new_time - old_time);

    int do_new;
    if (time > old_time-eps && time < old_time+eps)
    {
        do_new = 0;
    }
    else if (time > new_time-eps && time < new_time+eps)
    {
        do_new = 1;
    }
    else
    {
        BoxLib::Error("AmrLevel::setPhysBndryValues(): invalid time");
    }

    state[state_indx].FillBoundary(geom.CellSize(),
                                   geom.ProbDomain(),
                                   comp,
                                   ncomp,
                                   do_new);
}

FillPatchIteratorHelper::FillPatchIteratorHelper (AmrLevel& amrlevel,
                                                  MultiFab& leveldata)
    :
    MFIter(leveldata),
    m_amrlevel(amrlevel),
    m_leveldata(leveldata),
    m_mfid(m_amrlevel.level+1),
    m_finebox(m_leveldata.boxArray().size()),
    m_crsebox(m_leveldata.boxArray().size()),
    m_fbid(m_leveldata.boxArray().size()),
    m_ba(m_leveldata.boxArray().size()),
    m_init(false)
{}

FillPatchIterator::FillPatchIterator (AmrLevel& amrlevel,
                                      MultiFab& leveldata)
    :
    MFIter(leveldata),
    m_amrlevel(amrlevel),
    m_leveldata(leveldata),
    m_fph(PArrayManage),
    m_ncomp(0)
{}

FillPatchIteratorHelper::FillPatchIteratorHelper (AmrLevel&     amrlevel,
                                                  MultiFab&     leveldata,
                                                  int           boxGrow,
                                                  Real          time,
                                                  int           index,
                                                  int           scomp,
                                                  int           ncomp,
                                                  Interpolater* mapper)
    :
    MFIter(leveldata),
    m_amrlevel(amrlevel),
    m_leveldata(leveldata),
    m_mfid(m_amrlevel.level+1),
    m_finebox(m_leveldata.boxArray().size()),
    m_crsebox(m_leveldata.boxArray().size()),
    m_fbid(m_leveldata.boxArray().size()),
    m_ba(m_leveldata.boxArray().size()),
    m_time(time),
    m_growsize(boxGrow),
    m_index(index),
    m_scomp(scomp),
    m_ncomp(ncomp),
    m_init(false)
{
    Initialize(boxGrow,time,index,scomp,ncomp,mapper);
}

FillPatchIterator::FillPatchIterator (AmrLevel& amrlevel,
                                      MultiFab& leveldata,
                                      int       boxGrow,
                                      Real      time,
                                      int       index,
                                      int       scomp,
                                      int       ncomp)
    :
    MFIter(leveldata),
    m_amrlevel(amrlevel),
    m_leveldata(leveldata),
    m_fph(PArrayManage),
    m_ncomp(ncomp)
{
    BL_ASSERT(scomp >= 0);
    BL_ASSERT(ncomp >= 1);
    BL_ASSERT(AmrLevel::desc_lst[index].inRange(scomp,ncomp));
    BL_ASSERT(0 <= index && index < AmrLevel::desc_lst.size());

    Initialize(boxGrow,time,index,scomp,ncomp);
}

static
bool
NeedToTouchUpPhysCorners (const Geometry& geom)
{
    int n = 0;

    for (int dir = 0; dir < BL_SPACEDIM; dir++)
        if (geom.isPeriodic(dir))
            n++;

    return geom.isAnyPeriodic() && n < BL_SPACEDIM;
}

void
FillPatchIteratorHelper::Initialize (int           boxGrow,
                                     Real          time,
                                     int           index,
                                     int           scomp,
                                     int           ncomp,
                                     Interpolater* mapper)
{
    BL_PROFILE(BL_PROFILE_THIS_NAME() + "::Initialize()");
    BL_ASSERT(mapper);
    BL_ASSERT(scomp >= 0);
    BL_ASSERT(ncomp >= 1);
    BL_ASSERT(AmrLevel::desc_lst[index].inRange(scomp,ncomp));
    BL_ASSERT(0 <= index && index < AmrLevel::desc_lst.size());

    m_map          = mapper;
    m_time         = time;
    m_growsize     = boxGrow;
    m_index        = index;
    m_scomp        = scomp;
    m_ncomp        = ncomp;
    m_FixUpCorners = NeedToTouchUpPhysCorners(m_amrlevel.geom);

    const int         MyProc     = ParallelDescriptor::MyProc();
    PArray<AmrLevel>& amrLevels  = m_amrlevel.parent->getAmrLevels();
    const AmrLevel&   topLevel   = amrLevels[m_amrlevel.level];
    const Box&        topPDomain = topLevel.state[m_index].getDomain();
    const IndexType   boxType    = m_leveldata.boxArray()[0].ixType();
    const bool        extrap     = AmrLevel::desc_lst[m_index].extrap();
    //
    // Check that the interpolaters are identical.
    //
    BL_ASSERT(AmrLevel::desc_lst[m_index].identicalInterps(scomp,ncomp));

    for (int l = 0; l <= m_amrlevel.level; ++l)
    {
        amrLevels[l].state[m_index].RegisterData(m_mfcd, m_mfid[l]);
    }
    for (int i = 0; i < m_ba.size(); ++i)
    {
        if (m_leveldata.DistributionMap()[i] == MyProc)
        {
            m_ba.set(i, m_leveldata.boxArray()[i]);
            m_fbid[i].resize(m_amrlevel.level + 1);
            m_finebox[i].resize(m_amrlevel.level + 1);
            m_crsebox[i].resize(m_amrlevel.level + 1);
        }
    }
    m_ba.grow(m_growsize);  // These are the ones we want to fillpatch.

    BoxList unfillableThisLevel(boxType), tempUnfillable(boxType);
    std::vector<Box> unfilledThisLevel, crse_boxes;

    Array<IntVect> pshifts(27);

    for (int ibox = 0; ibox < m_ba.size(); ++ibox)
    {
        if (m_leveldata.DistributionMap()[ibox] != MyProc)
            continue;

        unfilledThisLevel.clear();
        unfilledThisLevel.push_back(m_ba[ibox]);

        if (!topPDomain.contains(m_ba[ibox]))
        {
            unfilledThisLevel.back() &= topPDomain;

            if (topLevel.geom.isAnyPeriodic())
            {
                //
                // May need to add additional unique pieces of valid region
                // in order to do periodic copies into ghost cells.
                //
                topLevel.geom.periodicShift(topPDomain,m_ba[ibox],pshifts);

                for (int iiv = 0; iiv < pshifts.size(); iiv++)
                {
                    Box shbox  = m_ba[ibox] + pshifts[iiv];
                    shbox     &= topPDomain;

                    if (boxType.nodeCentered())
                    {
                        for (int dir = 0; dir < BL_SPACEDIM; dir++)
                        {
                            if (pshifts[iiv][dir] > 0)
                                shbox.growHi(dir,-1);
                            else if (pshifts[iiv][dir] < 0)
                                shbox.growLo(dir,-1);
                        }
                    }

                    if (shbox.ok())
                    {
                        BoxList bl = BoxLib::boxDiff(shbox,m_ba[ibox]);
                        for (BoxList::iterator bli = bl.begin();
                             bli != bl.end();
                             ++bli)
                        {
                            unfilledThisLevel.push_back(*bli);
                        }
                    }
                }
            }
        }

        bool Done = false;

        for (int l = m_amrlevel.level; l >= 0 && !Done; --l)
        {
            unfillableThisLevel.clear();

            StateData&      theState      = amrLevels[l].state[m_index];
            const Box&      thePDomain    = theState.getDomain();
            const Geometry& theGeom       = amrLevels[l].geom;
            const bool      is_periodic   = theGeom.isAnyPeriodic();
            const IntVect&  fine_ratio    = amrLevels[l].fine_ratio;
            //
            // These are the boxes on this level contained in thePDomain
            // that need to be filled in order to directly fill at the
            // highest level or to interpolate up to the next higher level.
            //
            m_finebox[ibox][l].resize(unfilledThisLevel.size());

            for (int i = 0; i < unfilledThisLevel.size(); i++)
                m_finebox[ibox][l][i] = unfilledThisLevel[i];
            //
            // Now build coarse boxes needed to interpolate to fine.
            //
            // If we're periodic and we're not at the finest level, we may
            // need to get some additional data at this level in order to
            // properly fill the CoarseBox()d versions of the fineboxes.
            //
            crse_boxes.clear();

            const Array<Box>& FineBoxes = m_finebox[ibox][l];

            for (int i = 0; i < FineBoxes.size(); i++)
            {
                crse_boxes.push_back(FineBoxes[i]);

                if (l != m_amrlevel.level)
                {
		    Box cbox = m_map->CoarseBox(FineBoxes[i],fine_ratio);
		    crse_boxes.back() = cbox;
                    if (is_periodic && !thePDomain.contains(cbox))
                    {
                        theGeom.periodicShift(thePDomain,cbox,pshifts);

                        for (int iiv = 0; iiv < pshifts.size(); iiv++)
                        {
                            Box shbox = cbox + pshifts[iiv];
                            shbox    &= thePDomain;

                            if (boxType.nodeCentered())
                            {
                                for (int dir = 0; dir < BL_SPACEDIM; dir++)
                                {
                                    if (pshifts[iiv][dir] > 0)
                                        shbox.growHi(dir,-1);
                                    else if (pshifts[iiv][dir] < 0)
                                        shbox.growLo(dir,-1);
                                }
                            }

                            if (shbox.ok())
                                crse_boxes.push_back(shbox);
                        }
                    }
                }
            }

            m_crsebox[ibox][l].resize(crse_boxes.size());

            m_fbid[ibox][l].resize(crse_boxes.size());
            //
            // Now attempt to get as much coarse data as possible.
            //
            Array<Box>& CrseBoxes = m_crsebox[ibox][l];

            for (int i = 0; i < CrseBoxes.size(); i++)
            {
                BL_ASSERT(tempUnfillable.isEmpty());

                CrseBoxes[i] = crse_boxes[i];

                BL_ASSERT(CrseBoxes[i].intersects(thePDomain));

                theState.linInterpAddBox(m_mfcd,
                                         m_mfid[l],
                                         &tempUnfillable,
                                         m_fbid[ibox][l][i],
                                         CrseBoxes[i],
                                         m_time,
                                         m_scomp,
                                         0,
                                         m_ncomp,
                                         extrap);

                unfillableThisLevel.catenate(tempUnfillable);
            }

            unfillableThisLevel.intersect(thePDomain);

            if (unfillableThisLevel.isEmpty())
            {
                Done = true;
            }
            else
            {
                unfilledThisLevel.clear();

                for (BoxList::iterator bli = unfillableThisLevel.begin();
                     bli != unfillableThisLevel.end();
                     ++bli)
                {
                    unfilledThisLevel.push_back(*bli);
                }
            }
        }
    }

    m_mfcd.CollectData();

    m_init = true;
}

void
FillPatchIterator::Initialize (int  boxGrow,
                               Real time,
                               int  index,
                               int  scomp,
                               int  ncomp)
{
    BL_PROFILE(BL_PROFILE_THIS_NAME() + "::Initialize()");

    BL_ASSERT(scomp >= 0);
    BL_ASSERT(ncomp >= 1);
    BL_ASSERT(0 <= index && index < AmrLevel::desc_lst.size());

    const StateDescriptor& desc = AmrLevel::desc_lst[index];

    m_ncomp = ncomp;
    m_range = desc.sameInterps(scomp,ncomp);

    m_fph.resize(m_range.size());

    for (int i = 0; i < m_range.size(); i++)
    {
        const int SComp = m_range[i].first;
        const int NComp = m_range[i].second;

        m_fph.set(i,new FillPatchIteratorHelper(m_amrlevel,
                                                m_leveldata,
                                                boxGrow,
                                                time,
                                                index,
                                                SComp,
                                                NComp,
                                                desc.interp(SComp)));
    }

    BoxList bl(m_leveldata.boxArray()[0].ixType());

    for (int i = 0; i < m_leveldata.boxArray().size(); i++)
        bl.push_back(BoxLib::grow(m_leveldata.boxArray()[i], boxGrow));

    BoxArray nba(bl);

    m_fabs.define(nba,m_ncomp,0,Fab_allocate);

    BL_ASSERT(m_leveldata.DistributionMap() == m_fabs.DistributionMap());
    //
    // Now fill all our FABs.
    //
    for (MFIter mfi(m_fabs); mfi.isValid(); ++mfi)
    {
        int DComp = 0;

        for (int i = 0; i < m_range.size(); i++)
        {
            m_fph[i].fill(m_fabs[mfi.index()],DComp,mfi.index());

            DComp += m_range[i].second;
        }

        BL_ASSERT(DComp == m_ncomp);
    }
    //
    // Call hack to touch up fillPatched data
    //
    m_amrlevel.set_preferred_boundary_values(m_fabs,
                                             index,
                                             scomp,
                                             0,
                                             ncomp,
                                             time);
}

static
bool
HasPhysBndry (const Box&      b,
              const Box&      dmn,
              const Geometry& geom)
{
    for (int i = 0; i < BL_SPACEDIM; i++)
    {
        if (!geom.isPeriodic(i))
        {
            if (b.smallEnd(i) < dmn.smallEnd(i) || b.bigEnd(i) > dmn.bigEnd(i))
            {
                return true;
            }
        }
    }

    return false;
}

static
void
FixUpPhysCorners (FArrayBox&      fab,
                  StateData&      TheState,
                  const Geometry& TheGeom,
                  Real            time,
                  int             scomp,
                  int             dcomp,
                  int             ncomp)
{
    BL_PROFILE("FixUpPhysCorners");

    const Box& ProbDomain = TheState.getDomain();

    if (!HasPhysBndry(fab.box(),ProbDomain,TheGeom)) return;

    FArrayBox tmp;

    Box GrownDomain = ProbDomain;

    for (int dir = 0; dir < BL_SPACEDIM; dir++)
    {
        if (!TheGeom.isPeriodic(dir))
        {
            int lo = ProbDomain.smallEnd(dir) - fab.box().smallEnd(dir);
            int hi = fab.box().bigEnd(dir)    - ProbDomain.bigEnd(dir);
            if (lo > 0) GrownDomain.growLo(dir,lo);
            if (hi > 0) GrownDomain.growHi(dir,hi);
        }
    }

    for (int dir = 0; dir < BL_SPACEDIM; dir++)
    {
        if (!TheGeom.isPeriodic(dir)) continue;

        Box lo_slab = fab.box();
        Box hi_slab = fab.box();
        lo_slab.shift(dir, ProbDomain.length(dir));
        hi_slab.shift(dir,-ProbDomain.length(dir));
        lo_slab &= GrownDomain;
        hi_slab &= GrownDomain;

        if (lo_slab.ok())
        {
            lo_slab.shift(dir,-ProbDomain.length(dir));

            BL_ASSERT(fab.box().contains(lo_slab));
            BL_ASSERT(HasPhysBndry(lo_slab,ProbDomain,TheGeom));

            tmp.resize(lo_slab,ncomp);
            tmp.copy(fab,dcomp,0,ncomp);
            tmp.shift(dir,ProbDomain.length(dir));
            TheState.FillBoundary(tmp,
                                  time,
                                  TheGeom.CellSize(),
                                  TheGeom.ProbDomain(),
                                  0,
                                  scomp,
                                  ncomp);
            tmp.shift(dir,-ProbDomain.length(dir));
            fab.copy(tmp,0,dcomp,ncomp);
        }

        if (hi_slab.ok())
        {
            hi_slab.shift(dir,ProbDomain.length(dir));

            BL_ASSERT(fab.box().contains(hi_slab));
            BL_ASSERT(HasPhysBndry(hi_slab,ProbDomain,TheGeom));

            tmp.resize(hi_slab,ncomp);
            tmp.copy(fab,dcomp,0,ncomp);
            tmp.shift(dir,-ProbDomain.length(dir));
            TheState.FillBoundary(tmp,
                                  time,
                                  TheGeom.CellSize(),
                                  TheGeom.ProbDomain(),
                                  0,
                                  scomp,
                                  ncomp);
            tmp.shift(dir,ProbDomain.length(dir));
            fab.copy(tmp,0,dcomp,ncomp);
        }
    }
}

void
FillPatchIteratorHelper::fill (FArrayBox& fab,
                               int        dcomp,
                               int        idx)
{
    BL_ASSERT(fab.box() == m_ba[idx]);
    BL_ASSERT(fab.nComp() >= dcomp + m_ncomp);

    Array<IntVect>             pshifts(27);
    Array<BCRec>               bcr(m_ncomp);
    Array< PArray<FArrayBox> > cfab(m_amrlevel.level+1);
    const bool                 extrap    = AmrLevel::desc_lst[m_index].extrap();
    PArray<AmrLevel>&          amrLevels = m_amrlevel.parent->getAmrLevels();

#ifndef NDEBUG
    //
    // Set to special value we'll later check to ensure we've filled the FAB.
    //
    fab.setVal(2.e200,fab.box(),dcomp,m_ncomp);
#endif
    //
    // Build all coarse fabs from which we'll interpolate and
    // fill them with coarse data as best we can.
    //
    for (int l = 0; l <= m_amrlevel.level; l++)
    {
        StateData&         TheState = amrLevels[l].state[m_index];
        PArray<FArrayBox>& CrseFabs = cfab[l];

        cfab[l].resize(m_crsebox[idx][l].size(),PArrayManage);

        for (int i = 0; i < CrseFabs.size(); i++)
        {
            const Box& cbox = m_crsebox[idx][l][i];

            BL_ASSERT(cbox.ok());
            //
            // Set to special value we'll later check
            // to ensure we've filled the FABs at the coarse level.
            //
            CrseFabs.set(i, new FArrayBox(cbox,m_ncomp));

#ifndef NDEBUG
            CrseFabs[i].setVal(3.e200);
#endif
            TheState.linInterpFillFab(m_mfcd,
                                      m_mfid[l],
                                      m_fbid[idx][l][i],
                                      CrseFabs[i],
                                      m_time,
                                      0,
                                      0,
                                      m_ncomp,
                                      extrap);
        }
    }
    //
    // Now work from the bottom up interpolating to next higher level.
    //
    for (int l = 0; l < m_amrlevel.level; l++)
    {
        StateData&         TheState   = amrLevels[l].state[m_index];
        PArray<FArrayBox>& CrseFabs   = cfab[l];
        const Geometry&    TheGeom    = amrLevels[l].geom;
        const Box&         ThePDomain = TheState.getDomain();

        if (TheGeom.isAnyPeriodic())
        {
            //
            // Fill CrseFabs with periodic data in preparation for interp().
            //
            for (int i = 0; i < CrseFabs.size(); i++)
            {
                FArrayBox& dstfab = CrseFabs[i];

                if (!ThePDomain.contains(dstfab.box()))
                {
                    TheGeom.periodicShift(ThePDomain,dstfab.box(),pshifts);

                    for (int iiv = 0; iiv < pshifts.size(); iiv++)
                    {
                        Box fullsrcbox = dstfab.box() + pshifts[iiv];
                        fullsrcbox    &= ThePDomain;

                        for (int j = 0; j < CrseFabs.size(); j++)
                        {
                            FArrayBox& srcfab = CrseFabs[j];
                            Box        srcbox = fullsrcbox & srcfab.box();

                            if (srcbox.ok())
                            {
                                Box dstbox = srcbox - pshifts[iiv];

                                dstfab.copy(srcfab,srcbox,0,dstbox,0,m_ncomp);
                            }
                        }
                    }
                }
            }
        }
        //
        // Set non-periodic BCs in coarse data -- what we interpolate with.
        // This MUST come after the periodic fill mumbo-jumbo.
        //
	const Real*    theCellSize   = TheGeom.CellSize();
	const RealBox& theProbDomain = TheGeom.ProbDomain();

        for (int i = 0; i < CrseFabs.size(); i++)
        {
            if (!ThePDomain.contains(CrseFabs[i].box()))
            {
                TheState.FillBoundary(CrseFabs[i],
                                      m_time,
                                      theCellSize,
                                      theProbDomain,
                                      0,
                                      m_scomp,
                                      m_ncomp);
            }
            //
            // The coarse FAB had better be completely filled with "good" data.
            //
            BL_ASSERT(CrseFabs[i].norm(0,0,m_ncomp) < 3.e200);
        }

        if (m_FixUpCorners)
        {
            for (int i = 0; i < CrseFabs.size(); i++)
                FixUpPhysCorners(CrseFabs[i],TheState,TheGeom,m_time,m_scomp,0,m_ncomp);
        }
        //
        // Interpolate up to next level.
        //
        const IntVect&      fine_ratio    = amrLevels[l].fine_ratio;
        const Array<Box>&   FineBoxes     = m_finebox[idx][l];
        StateData&          fState        = amrLevels[l+1].state[m_index];
        const Box&          fDomain       = fState.getDomain();
        PArray<FArrayBox>&  FinerCrseFabs = cfab[l+1];
        const Array<BCRec>& theBCs        = AmrLevel::desc_lst[m_index].getBCs();

        FArrayBox finefab, crsefab;

        for (int i = 0; i < FineBoxes.size(); i++)
        {
            finefab.resize(FineBoxes[i],m_ncomp);

            Box crse_box = m_map->CoarseBox(finefab.box(),fine_ratio);

            crsefab.resize(crse_box,m_ncomp);
            //
            // Fill crsefab from m_crsebox via copy on intersect.
            //
            for (int j = 0; j < CrseFabs.size(); j++)
                crsefab.copy(CrseFabs[j]);
            //
            // Get boundary conditions for the fine patch.
            //
            BoxLib::setBC(finefab.box(),
                          fDomain,
                          m_scomp,
                          0,
                          m_ncomp,
                          theBCs,
                          bcr);
            //
            // The coarse FAB had better be completely filled with "good" data.
            //
            BL_ASSERT(crsefab.norm(0,0,m_ncomp) < 3.e200);
            //
            // Interpolate up to fine patch.
            //
            m_map->interp(crsefab,
                          0,
                          finefab,
                          0,
                          m_ncomp,
                          finefab.box(),
                          fine_ratio,
                          amrLevels[l].geom,
                          amrLevels[l+1].geom,
                          bcr);
            //
            // Copy intersect finefab into next level m_crseboxes.
            //
            for (int j = 0; j < FinerCrseFabs.size(); j++)
                FinerCrseFabs[j].copy(finefab);
        }
        //
        // No longer need coarse data at this level.
        //
        CrseFabs.clear();
    }
    //
    // Now for the finest level stuff.
    //
    StateData&         FineState      = m_amrlevel.state[m_index];
    const Box&         FineDomain     = FineState.getDomain();
    const Geometry&    FineGeom       = m_amrlevel.geom;
    PArray<FArrayBox>& FinestCrseFabs = cfab[m_amrlevel.level];
    //
    // Copy intersect coarse into destination fab.
    //
    for (int i = 0; i < FinestCrseFabs.size(); i++)
        fab.copy(FinestCrseFabs[i],0,dcomp,m_ncomp);

    if (FineGeom.isAnyPeriodic() && !FineDomain.contains(fab.box()))
    {
        FineGeom.periodicShift(FineDomain,fab.box(),pshifts);

        for (int i = 0; i < FinestCrseFabs.size(); i++)
        {
            for (int iiv = 0; iiv < pshifts.size(); iiv++)
            {
                fab.shift(pshifts[iiv]);

                Box src_dst = FinestCrseFabs[i].box() & fab.box();
                src_dst    &= FineDomain;

                if (src_dst.ok())
                    fab.copy(FinestCrseFabs[i],src_dst,0,src_dst,dcomp,m_ncomp);

                fab.shift(-pshifts[iiv]);
            }
        }
    }
    //
    // No longer need coarse data at finest level.
    //
    FinestCrseFabs.clear();
    //
    // Final set of non-periodic BCs.
    //
    if (!FineState.getDomain().contains(fab.box()))
    {
        FineState.FillBoundary(fab,
                               m_time,
                               FineGeom.CellSize(),
                               FineGeom.ProbDomain(),
                               dcomp,
                               m_scomp,
                               m_ncomp);
    }

    if (m_FixUpCorners)
    {
        FixUpPhysCorners(fab,FineState,FineGeom,m_time,m_scomp,dcomp,m_ncomp);
    }
}

bool
FillPatchIteratorHelper::isValid ()
{
    BL_ASSERT(m_init);

    return MFIter::isValid() ? true : false;
}

void
FillPatchIterator::operator++ ()
{
    MFIter::operator++();

    for (int i = 0; i < m_fph.size(); i++) ++m_fph[i];
}

bool
FillPatchIterator::isValid ()
{
    BL_ASSERT(m_ncomp > 0);
    BL_ASSERT(m_fph.size() == m_range.size());

    if (!MFIter::isValid()) return false;

    for (int i = 0; i < m_fph.size(); i++)
    {
        bool result = m_fph[i].isValid();

        BL_ASSERT(result == true);
    }

    return true;
}

FillPatchIteratorHelper::~FillPatchIteratorHelper () {}

FillPatchIterator::~FillPatchIterator () {}

void
AmrLevel::FillCoarsePatch (MultiFab& mf,
                           int       dcomp,
                           Real      time,
                           int       index,
                           int       scomp,
                           int       ncomp)
{
    BL_PROFILE(BL_PROFILE_THIS_NAME() + "::FillCoarsePatch()");
    //
    // Must fill this region on crse level and interpolate.
    //
    BL_ASSERT(level != 0);
    BL_ASSERT(ncomp <= (mf.nComp()-dcomp));
    BL_ASSERT(0 <= index && index < desc_lst.size());

    Array<BCRec>            bcr(ncomp);
    int                     DComp   = dcomp;
    const StateDescriptor&  desc    = desc_lst[index];
    const Box&              pdomain = state[index].getDomain();
    const BoxArray&         mf_BA   = mf.boxArray();
    AmrLevel&               clev    = parent->getLevel(level-1);

    std::vector< std::pair<int,int> > ranges  = desc.sameInterps(scomp,ncomp);

    BL_ASSERT(desc.inRange(scomp, ncomp));

    for (int i = 0; i < ranges.size(); i++)
    {
        const int     SComp  = ranges[i].first;
        const int     NComp  = ranges[i].second;
        Interpolater* mapper = desc.interp(SComp);

        BoxArray crseBA(mf_BA.size());
        
        for (int j = 0; j < crseBA.size(); ++j)
        {
            BL_ASSERT(mf_BA[j].ixType() == desc.getType());

            crseBA.set(j,mapper->CoarseBox(mf_BA[j],crse_ratio));
        }

        MultiFab crseMF(crseBA,NComp,0,Fab_noallocate);

        FillPatchIterator fpi(clev,crseMF,0,time,index,SComp,NComp);

        for ( ; fpi.isValid(); ++fpi)
        {
            const Box& dbox = mf_BA[fpi.index()];

            BoxLib::setBC(dbox,pdomain,SComp,0,NComp,desc.getBCs(),bcr);

            mapper->interp(fpi(),
                           0,
                           mf[fpi],
                           DComp,
                           NComp,
                           dbox,
                           crse_ratio,
                           clev.geom,
                           geom,
                           bcr);
        }

        DComp += NComp;
    }
}

MultiFab*
AmrLevel::derive (const std::string& name,
                  Real           time,
                  int            ngrow)
{
    BL_ASSERT(ngrow >= 0);

    MultiFab* mf = 0;

    int index, scomp, ncomp;

    if (isStateVariable(name, index, scomp))
    {
        mf = new MultiFab(state[index].boxArray(), 1, ngrow);

        FillPatchIterator fpi(*this,get_new_data(index),ngrow,time,index,scomp,1);

        for ( ; fpi.isValid(); ++fpi)
        {
            BL_ASSERT((*mf)[fpi].box() == fpi().box());

            (*mf)[fpi].copy(fpi());
        }
    }
    else if (const DeriveRec* rec = derive_lst.get(name))
    {
        rec->getRange(0, index, scomp, ncomp);

        BoxArray srcBA(state[index].boxArray());
        BoxArray dstBA(state[index].boxArray());

        srcBA.convert(rec->boxMap());
        dstBA.convert(rec->deriveType());

        MultiFab srcMF(srcBA, rec->numState(), ngrow);

        for (int k = 0, dc = 0; k < rec->numRange(); k++, dc += ncomp)
        {
            rec->getRange(k, index, scomp, ncomp);

            FillPatchIterator fpi(*this,srcMF,ngrow,time,index,scomp,ncomp);

            for ( ; fpi.isValid(); ++fpi)
            {
                srcMF[fpi].copy(fpi(), 0, dc, ncomp);
            }
        }

        mf = new MultiFab(dstBA, rec->numDerive(), ngrow);

        for (MFIter mfi(srcMF); mfi.isValid(); ++mfi)
        {
            int         grid_no = mfi.index();
            Real*       ddat    = (*mf)[grid_no].dataPtr();
            const int*  dlo     = (*mf)[grid_no].loVect();
            const int*  dhi     = (*mf)[grid_no].hiVect();
            int         n_der   = rec->numDerive();
            Real*       cdat    = srcMF[mfi].dataPtr();
            const int*  clo     = srcMF[mfi].loVect();
            const int*  chi     = srcMF[mfi].hiVect();
            int         n_state = rec->numState();
            const int*  dom_lo  = state[index].getDomain().loVect();
            const int*  dom_hi  = state[index].getDomain().hiVect();
            const Real* dx      = geom.CellSize();
            const int*  bcr     = rec->getBC();
            const Real* xlo     = grid_loc[grid_no].lo();
            Real        dt      = parent->dtLevel(level);

            rec->derFunc()(ddat,ARLIM(dlo),ARLIM(dhi),&n_der,
                           cdat,ARLIM(clo),ARLIM(chi),&n_state,
                           dlo,dhi,dom_lo,dom_hi,dx,xlo,&time,&dt,bcr,
                           &level,&grid_no);
        }
    }
    else
    {
        //
        // If we got here, cannot derive given name.
        //
        std::string msg("AmrLevel::derive(MultiFab*): unknown variable: ");
        msg += name;
        BoxLib::Error(msg.c_str());
    }

    return mf;
}

void
AmrLevel::derive (const std::string& name,
                  Real           time,
                  MultiFab&      mf,
                  int            dcomp)
{
    BL_ASSERT(dcomp < mf.nComp());

    const int ngrow = mf.nGrow();

    int index, scomp, ncomp;

    if (isStateVariable(name,index,scomp))
    {
        FillPatchIterator fpi(*this,mf,ngrow,time,index,scomp,1);

        for ( ; fpi.isValid(); ++fpi)
        {
            BL_ASSERT(mf[fpi].box() == fpi().box());

            mf[fpi].copy(fpi(),0,dcomp,1);
        }
    }
    else if (const DeriveRec* rec = derive_lst.get(name))
    {
        rec->getRange(0,index,scomp,ncomp);

        BoxArray srcBA(mf.boxArray());
        BoxArray dstBA(mf.boxArray());

        srcBA.convert(state[index].boxArray()[0].ixType());
        BL_ASSERT(rec->deriveType() == dstBA[0].ixType());

        MultiFab srcMF(srcBA,rec->numState(),ngrow);

        for (int k = 0, dc = 0; k < rec->numRange(); k++, dc += ncomp)
        {
            rec->getRange(k,index,scomp,ncomp);

            FillPatchIterator fpi(*this,srcMF,ngrow,time,index,scomp,ncomp);

            for ( ; fpi.isValid(); ++fpi)
            {
                BL_ASSERT(srcMF[fpi].box() == fpi().box());

                srcMF[fpi].copy(fpi(),0,dc,ncomp);
            }
        }

        for (MFIter mfi(srcMF); mfi.isValid(); ++mfi)
        {
            int         idx     = mfi.index();
            Real*       ddat    = mf[idx].dataPtr(dcomp);
            const int*  dlo     = mf[idx].loVect();
            const int*  dhi     = mf[idx].hiVect();
            int         n_der   = rec->numDerive();
            Real*       cdat    = srcMF[mfi].dataPtr();
            const int*  clo     = srcMF[mfi].loVect();
            const int*  chi     = srcMF[mfi].hiVect();
            int         n_state = rec->numState();
            const int*  dom_lo  = state[index].getDomain().loVect();
            const int*  dom_hi  = state[index].getDomain().hiVect();
            const Real* dx      = geom.CellSize();
            const int*  bcr     = rec->getBC();
            const RealBox temp  = RealBox(mf[idx].box(),geom.CellSize(),geom.ProbLo());
            const Real* xlo     = temp.lo();
            Real        dt      = parent->dtLevel(level);

            rec->derFunc()(ddat,ARLIM(dlo),ARLIM(dhi),&n_der,
                           cdat,ARLIM(clo),ARLIM(chi),&n_state,
                           dlo,dhi,dom_lo,dom_hi,dx,xlo,&time,&dt,bcr,
                           &level,&idx);
        }
    }
    else
    {
        //
        // If we got here, cannot derive given name.
        //
        std::string msg("AmrLevel::derive(MultiFab*): unknown variable: ");
        msg += name;
        BoxLib::Error(msg.c_str());
    }
}

Array<int>
AmrLevel::getBCArray (int State_Type,
                      int gridno,
                      int strt_comp,
                      int ncomp)
{
    Array<int> bc(2*BL_SPACEDIM*ncomp);

    for (int n = 0; n < ncomp; n++)
    {
        const int* b_rec = state[State_Type].getBC(strt_comp+n,gridno).vect();
        for (int m = 0; m < 2*BL_SPACEDIM; m++)
            bc[2*BL_SPACEDIM*n + m] = b_rec[m];
    }

    return bc;
}

int
AmrLevel::okToRegrid ()
{
    return true;
}

void
AmrLevel::setPlotVariables ()
{
    ParmParse pp("amr");

    if (pp.contains("plot_vars"))
    {
        std::string nm;
      
        int nPltVars = pp.countval("plot_vars");
      
        for (int i = 0; i < nPltVars; i++)
        {
            pp.get("plot_vars", nm, i);

            if (nm == "ALL") 
                parent->fillStatePlotVarList();
            else if (nm == "NONE")
                parent->clearStatePlotVarList();
            else
                parent->addStatePlotVar(nm);
        }
    }
    else 
    {
        //
        // The default is to add them all.
        //
        parent->fillStatePlotVarList();
    }
  
    if (pp.contains("derive_plot_vars"))
    {
        std::string nm;
      
        int nDrvPltVars = pp.countval("derive_plot_vars");
      
        for (int i = 0; i < nDrvPltVars; i++)
        {
            pp.get("derive_plot_vars", nm, i);

            if (nm == "ALL") 
                parent->fillDerivePlotVarList();
            else if (nm == "NONE")
                parent->clearDerivePlotVarList();
            else
                parent->addDerivePlotVar(nm);
        }
    }
    else 
    {
        //
        // The default is to add none of them.
        //
        parent->clearDerivePlotVarList();
    }
}

AmrLevel::TimeLevel
AmrLevel::which_time (int  indx,
                      Real time) const
{
    const Real oldtime = state[indx].prevTime();
    const Real newtime = state[indx].curTime();
    const Real haftime = .5 * (oldtime + newtime);
    const Real qtime = oldtime + 0.25*(newtime-oldtime);
    const Real tqtime = oldtime + 0.75*(newtime-oldtime);
    const Real epsilon = 0.001 * (newtime - oldtime);

    BL_ASSERT(time >= oldtime-epsilon && time <= newtime+epsilon);
    
    if (time >= oldtime-epsilon && time <= oldtime+epsilon)
    {
        return AmrOldTime;
    }
    else if (time >= newtime-epsilon && time <= newtime+epsilon)
    {
        return AmrNewTime;
    }
    else if (time >= haftime-epsilon && time <= haftime+epsilon)
    {
        return AmrHalfTime;
    }
    else if (time >= qtime-epsilon && time <= qtime+epsilon)
    {
        return Amr1QtrTime;
    }
    else if (time >= tqtime-epsilon && time <= tqtime+epsilon)
    {
        return Amr3QtrTime;
    }
    return AmrOtherTime;
}
