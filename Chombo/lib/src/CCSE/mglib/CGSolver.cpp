//
// $Id: CGSolver.cpp,v 1.1 2007-05-24 18:12:03 tdsternberg Exp $
//
#include <winstd.H>

#include <algorithm>
#include <iomanip>

#include <ParmParse.H>
#include <ParallelDescriptor.H>
#include <Profiler.H>
#include <Utility.H>
#include <CG_F.H>
#include <CGSolver.H>
#include <MultiGrid.H>

int              CGSolver::initialized            = 0;
int              CGSolver::def_maxiter            = 40;
int              CGSolver::def_verbose            = 0;
CGSolver::Solver CGSolver::def_cg_solver          = BiCGStab;
double           CGSolver::def_unstable_criterion = 10.;

static
void
Spacer (std::ostream& os, int lev)
{
    for (int k = 0; k < lev; k++)
    {
        os << "   ";
    }
}

void
CGSolver::initialize ()
{
    ParmParse pp("cg");

    pp.query("maxiter", def_maxiter);
    pp.query("v", def_verbose);
    pp.query("verbose", def_verbose);
    pp.query("unstable_criterion",def_unstable_criterion);
    int ii;
    if (pp.query("cg_solver", ii))
    {
        switch (ii)
        {
        case 0: def_cg_solver = CG; break;
        case 1: def_cg_solver = BiCGStab; break;
        case 2: def_cg_solver = CG_Alt; break;
        default:
            BoxLib::Error("CGSolver::initialize(): bad cg_solver");
        }
    }

    if (ParallelDescriptor::IOProcessor() && def_verbose)
    {
        std::cout << "CGSolver settings...\n";
	std::cout << "   def_maxiter            = " << def_maxiter << '\n';
	std::cout << "   def_unstable_criterion = " << def_unstable_criterion << '\n';
	std::cout << "   def_cg_solver = " << def_cg_solver << '\n';
    }
    
    initialized = 1;
}

CGSolver::CGSolver (LinOp& _Lp,
            bool   _use_mg_precond,
            int    _lev)
    :
    Lp(_Lp),
    mg_precond(0),
    lev(_lev),
    use_mg_precond(_use_mg_precond)
{
    if (!initialized)
        initialize();
    maxiter = def_maxiter;
    verbose = def_verbose;
    cg_solver = def_cg_solver;
    set_mg_precond();
}

void
CGSolver::set_mg_precond ()
{
    delete mg_precond;
    if (use_mg_precond)
    {
        mg_precond = new MultiGrid(Lp);
    }
}

CGSolver::~CGSolver ()
{
    delete mg_precond;
}

static
Real
norm_inf (const MultiFab& res)
{
    Real restot = 0.0;
    for (MFIter mfi(res); mfi.isValid(); ++mfi) 
    {
        restot = std::max(restot, res[mfi].norm(mfi.validbox(), 0));
    }
    ParallelDescriptor::ReduceRealMax(restot);
    return restot;
}

int
CGSolver::solve (MultiFab&       sol,
                 const MultiFab& rhs,
                 Real            eps_rel,
                 Real            eps_abs,
                 LinOp::BC_Mode  bc_mode,
		 Solver solver)
{
    if ( solver == Automatic ) solver = cg_solver;
    if ( solver == CG )       return solve_cg(sol, rhs, eps_rel, eps_abs, bc_mode);
    if ( solver == BiCGStab ) return solve_bicgstab(sol, rhs, eps_rel, eps_abs, bc_mode);
    if ( solver == CG_Alt )   return solve_00(sol, rhs, eps_rel, eps_abs, bc_mode);
    return solve_00(sol, rhs, eps_rel, eps_abs, bc_mode);
}

int
CGSolver::solve_00 (MultiFab&       sol,
                 const MultiFab& rhs,
                 Real            eps_rel,
                 Real            eps_abs,
                 LinOp::BC_Mode  bc_mode)
{
    BL_PROFILE(BL_PROFILE_THIS_NAME() + "::solve_00()");
    //
    // algorithm:
    //
    //   k=0;r=rhs-A*soln_0;
    //   while (||r_k||^2_2 > eps^2*||r_o||^2_2 && k < maxiter {
    //      k++
    //      solve Mz_k-1 = r_k-1 (if preconditioning, else z_k-1 = r_k-1)
    //      rho_k-1 = r_k-1^T z_k-1
    //      if (k=1) { p_1 = z_0 }
    //      else { beta = rho_k-1/rho_k-2; p = z + beta*p }
    //      w = Ap
    //      alpha = rho_k-1/p^tw
    //      x += alpha p
    //      r -= alpha w
    //   }
    //
    BL_ASSERT(sol.boxArray() == Lp.boxArray(lev));
    BL_ASSERT(rhs.boxArray() == Lp.boxArray(lev));

    int nghost = 1; int ncomp = sol.nComp();
    MultiFab* s = new MultiFab(sol.boxArray(), ncomp, nghost, Fab_allocate);
    MultiFab* r = new MultiFab(sol.boxArray(), ncomp, nghost, Fab_allocate);
    MultiFab* z = new MultiFab(sol.boxArray(), ncomp, nghost, Fab_allocate);
    MultiFab* w = new MultiFab(sol.boxArray(), ncomp, nghost, Fab_allocate);
    MultiFab* p = new MultiFab(sol.boxArray(), ncomp, nghost, Fab_allocate);
    //
    // Copy initial guess into a temp multifab guaranteed to have ghost cells.
    //
    int srccomp=0;  int destcomp=0;  ncomp=1;  nghost=0;
    s->copy(sol,srccomp,destcomp,ncomp);
    /*
      This routine assumes the LinOp is linear, and that when bc_mode =
      LinOp::Homogeneous_BC, LinOp::apply() on a zero vector will return a zero
      vector.  Given that, we define the problem we solve here from the
      original equation:

      Lp(sol) = rhs --> Lp(s) + Lp(sol,bc_mode=LinOp::Homogeneous_BC) = rhs

      where s is set to the incoming solution guess.  Rewriting,

      Lp(sol,bc_mode=LinOp::Homogeneous_BC) = r     [ = rhs - Lp(s) ].

      CG needs the residual of this equation on our initial guess.  But
      because we made the above assumption,

      r - Lp(sol,bc_mode=LinOp::Homogeneous_BC) = r = rhs - Lp(s)

      Which is simply the residual of the original equation evaluated at
      the initial guess.  Thus we get by with only one call to Lp.residual.
      Without this assumption, we'd need two.
    */
    Lp.residual((*r),  rhs, (*s), lev, bc_mode);
    //
    // Set initial guess for correction to 0.
    //
    sol.setVal(0.0);
    //
    // Set bc_mode=homogeneous
    //
    LinOp::BC_Mode temp_bc_mode=LinOp::Homogeneous_BC;

    Real rnorm  = norm_inf(*r);
    Real rnorm0 = rnorm;
    Real minrnorm = rnorm;
    int ret = 0; // will return this value if all goes well

    if (verbose > 0 && ParallelDescriptor::IOProcessor())
    {
        Spacer(std::cout, lev);
        std::cout << "CGsolver: Initial error (error0) =        " << rnorm0 << '\n';
    }

    Real beta, rho, rhoold = 0.0;
    /*
      The MultiFab copies used below to update z and p require nghost=0
      to avoid the possibility of filling valid regions with uninitialized
      data in the invalid regions of neighboring grids.  The default
      behavior in MultiFab copies will likely be changed in the future.
    */
    //
    // If eps_rel or eps_abs < 0: that test is effectively bypassed.
    //
    int nit = 1;
    for (;
         nit <= maxiter && rnorm > eps_rel*rnorm0 && rnorm > eps_abs;
         ++nit)
    {
        if (use_mg_precond)
        {
            //
            // Solve Mz_k-1 = r_k-1  and  rho_k-1 = r_k-1^T z_k-1
            //
            z->setVal(0.);
            mg_precond->solve(*z, *r, eps_rel, eps_abs, temp_bc_mode);
        }
        else
        {
            //
            // No preconditioner, z_k-1 = r_k-1  and  rho_k-1 = r_k-1^T r_k-1.
            //
            srccomp=0;  destcomp=0;  ncomp=1;
            z->copy((*r), srccomp, destcomp, ncomp);
        }

        rho = 0.0;
        int ncomp = z->nComp();
        const BoxArray& gbox = r->boxArray();

        for (MFIter rmfi(*r); rmfi.isValid(); ++rmfi)
        {
            Real trho;
            BL_ASSERT(rmfi.validbox() == gbox[rmfi.index()]);
            FORT_CGXDOTY(&trho,(*z)[rmfi].dataPtr(), 
                         ARLIM((*z)[rmfi].loVect()),ARLIM((*z)[rmfi].hiVect()),
                         (*r)[rmfi].dataPtr(), 
                         ARLIM((*r)[rmfi].loVect()),ARLIM((*r)[rmfi].hiVect()),
                         rmfi.validbox().loVect(),rmfi.validbox().hiVect(),
                         &ncomp);
            rho += trho;
        }
        ParallelDescriptor::ReduceRealSum(rho);

        if (nit == 1)
        {
            //
            // k=1, p_1 = z_0
            //
            srccomp=0;  destcomp=0;  ncomp=1;  nghost=0;
            p->copy(*z, srccomp, destcomp, ncomp);
        }
        else
        {
            //
            // k>1, beta = rho_k-1/rho_k-2 and  p = z + beta*p
            //
            beta = rho/rhoold;
            advance(*p, beta, *z);
        }
        //
        //  w = Ap, and compute Transpose(p).w
        //
        Real pw = axp(*w, *p, temp_bc_mode);
        //
        // alpha = rho_k-1/p^tw
        //
	Real alpha;
	if ( pw != 0. ){
	  alpha = rho/pw;
	}
	else {
	  ret = 1;
	  break;
	}
        
        if (ParallelDescriptor::IOProcessor() && verbose > 2)
        {
	  Spacer(std::cout, lev);
          std::cout << "CGSolver_00:"
                    << " nit " << nit
                    << " pw "  << pw 
                    << " rho " << rho
                    << " alpha " << alpha;
            if (nit == 1)
            {
                std::cout << " beta undefined ...";
            }
            else
            {
                std::cout << " beta " << beta << " ...";
            }
        }
        //
        // x += alpha p  and  r -= alpha w
        //
        rhoold = rho;
        update(sol, alpha, *r, *p, *w);
        rnorm = norm_inf(*r);
	if (rnorm > def_unstable_criterion*minrnorm)
        {
            ret = 2;
            break;
	}
	else if (rnorm < minrnorm)
        {
            minrnorm = rnorm;
	}

        if (ParallelDescriptor::IOProcessor())
        {
            if (verbose > 1 ||
                (((eps_rel > 0. && rnorm < eps_rel*rnorm0) ||
                  (eps_abs > 0. && rnorm < eps_abs)) && verbose))
            {
                const Real rel_error = (rnorm0 != 0) ? rnorm/rnorm0 : 0;
                Spacer(std::cout, lev);
                std::cout << "CGSolver_00: Iteration "
                          << std::setw(4) << nit
                          << " error/error0 "
                          << rel_error << '\n';
            }
        }
    }
    
    if (ParallelDescriptor::IOProcessor())
    {
	if (verbose > 0 ||
	    (((eps_rel > 0. && rnorm < eps_rel*rnorm0) ||
	      (eps_abs > 0. && rnorm < eps_abs)) && verbose))
        {
            const Real rel_error = (rnorm0 != 0) ? rnorm/rnorm0 : 0;
	    Spacer(std::cout, lev);
	    std::cout << "CGSolver_00: Final: Iteration "
                      << std::setw(4) << nit
                      << " error/error0 "
                      << rel_error << '\n';
        }
    }
    if (ret==0 && rnorm > eps_rel*rnorm0 && rnorm > eps_abs)
    {
      if ( ParallelDescriptor::IOProcessor() )
	{
	  BoxLib::Warning("CGSolver_00:: failed to converge!");
	}
      ret = 8;
    }
    //
    // Omit ghost update since maybe not initialized in calling routine.
    // BoxLib_1.99 has no MultiFab::plus(MultiFab&) member, which would
    // operate only in valid regions; do explicitly.  Add to boundary
    // values stored in initialsolution.
    //
    if ( ret == 0 || ret == 8 )
    {
        srccomp=0; ncomp=1; nghost=0;
        sol.plus(*s,srccomp,ncomp,nghost);
    }

    delete s;
    delete r;
    delete w;
    delete p;
    delete z;
    return ret;
}

void
CGSolver::advance (MultiFab&       p,
                   Real            beta,
                   const MultiFab& z)
{
    //
    // Compute p = z  +  beta p
    //
    const BoxArray& gbox = Lp.boxArray(lev);
    int ncomp = p.nComp();
    const BoxArray& zbox = z.boxArray();

    for (MFIter pmfi(p); pmfi.isValid(); ++pmfi)
    {
        BL_ASSERT(zbox[pmfi.index()] == gbox[pmfi.index()]);

        FORT_CGADVCP(p[pmfi].dataPtr(),
                     ARLIM(p[pmfi].loVect()), ARLIM(p[pmfi].hiVect()),
                     z[pmfi].dataPtr(),
                     ARLIM(z[pmfi].loVect()), ARLIM(z[pmfi].hiVect()),
                     &beta,
                     zbox[pmfi.index()].loVect(), zbox[pmfi.index()].hiVect(),
                     &ncomp);
    }
}

void
CGSolver::update (MultiFab&       sol,
                  Real            alpha,
                  MultiFab&       r,
                  const MultiFab& p,
                  const MultiFab& w)
{
    //
    // compute x =+ alpha p  and  r -= alpha w
    //
    const BoxArray& gbox = Lp.boxArray(lev);
    int ncomp = r.nComp();

    for (MFIter solmfi(sol); solmfi.isValid(); ++solmfi)
    {
        BL_ASSERT(solmfi.validbox() == gbox[solmfi.index()]);

        FORT_CGUPDATE(sol[solmfi].dataPtr(),
                      ARLIM(sol[solmfi].loVect()), ARLIM(sol[solmfi].hiVect()),
                      r[solmfi].dataPtr(),
                      ARLIM(r[solmfi].loVect()),   ARLIM(r[solmfi].hiVect()),
                      &alpha,
                      w[solmfi].dataPtr(),
                      ARLIM(w[solmfi].loVect()), ARLIM(w[solmfi].hiVect()),
                      p[solmfi].dataPtr(),
                      ARLIM(p[solmfi].loVect()), ARLIM(p[solmfi].hiVect()),
                      solmfi.validbox().loVect(), solmfi.validbox().hiVect(),
                      &ncomp);
    }
}

Real
CGSolver::axp (MultiFab&      w,
               MultiFab&      p,
               LinOp::BC_Mode bc_mode)
{
    //
    // Compute w = A.p, and return Transpose(p).w
    //
    Real pw = 0.0;
    const BoxArray& gbox = Lp.boxArray(lev);
    Lp.apply(w, p, lev, bc_mode);
    int ncomp = p.nComp();

    for (MFIter pmfi(p); pmfi.isValid(); ++pmfi)
    {
        Real tpw;
        BL_ASSERT(pmfi.validbox() == gbox[pmfi.index()]);
        FORT_CGXDOTY(&tpw,
                     p[pmfi].dataPtr(),
                     ARLIM(p[pmfi].loVect()), ARLIM(p[pmfi].hiVect()),
                     w[pmfi].dataPtr(),
                     ARLIM(w[pmfi].loVect()), ARLIM(w[pmfi].hiVect()),
                     pmfi.validbox().loVect(), pmfi.validbox().hiVect(),
                     &ncomp);
        pw += tpw;
    }

    ParallelDescriptor::ReduceRealSum(pw);

    return pw;
}

static
void
sxay (MultiFab& ss, const MultiFab& xx, Real a, const MultiFab& yy)
{
    const int ncomp = ss.nComp();

    for (MFIter smfi(ss); smfi.isValid(); ++smfi)
    {
        FORT_CGSXAY(ss[smfi].dataPtr(),
                    ARLIM(ss[smfi].loVect()), ARLIM(ss[smfi].hiVect()),
		    xx[smfi].dataPtr(),
                    ARLIM(xx[smfi].loVect()), ARLIM(xx[smfi].hiVect()),
		    &a,
		    yy[smfi].dataPtr(),
                    ARLIM(yy[smfi].loVect()), ARLIM(yy[smfi].hiVect()),
		    smfi.validbox().loVect(), smfi.validbox().hiVect(),
		    &ncomp);
    }
}

Real
dotxy (const MultiFab& r, const MultiFab& z)
{
    BL_PROFILE("CGSolver::dotxy");

    int ncomp = z.nComp();
    Real rho = 0.0;
    for (MFIter rmfi(r); rmfi.isValid(); ++rmfi)
    {
        Real trho;
        FORT_CGXDOTY(&trho,
                     z[rmfi].dataPtr(),
                     ARLIM(z[rmfi].loVect()),ARLIM(z[rmfi].hiVect()),
                     r[rmfi].dataPtr(),
                     ARLIM(r[rmfi].loVect()),ARLIM(r[rmfi].hiVect()),
                     rmfi.validbox().loVect(),rmfi.validbox().hiVect(),
                     &ncomp);
        rho += trho;
    }
    ParallelDescriptor::ReduceRealSum(rho);
    return rho;
}

int
CGSolver::solve_bicgstab (MultiFab&       sol,
		       const MultiFab& rhs,
		       Real            eps_rel,
		       Real            eps_abs,
		       LinOp::BC_Mode  bc_mode)
{
    BL_PROFILE(BL_PROFILE_THIS_NAME() + "::solve_bicgstab()");

    const int nghost = 1;
    const int ncomp  = 1;
    BL_ASSERT(sol.boxArray() == Lp.boxArray(lev));
    BL_ASSERT(rhs.boxArray() == Lp.boxArray(lev));
    BL_ASSERT(sol.nComp() == 1);

    MultiFab sorig(sol.boxArray(), ncomp, nghost);
    MultiFab s(sol.boxArray(), ncomp, nghost);
    MultiFab sh(sol.boxArray(), ncomp, nghost);
    MultiFab r(sol.boxArray(), ncomp, nghost);
    MultiFab rh(sol.boxArray(), ncomp, nghost);
    MultiFab p(sol.boxArray(), ncomp, nghost);
    MultiFab ph(sol.boxArray(), ncomp, nghost);
    MultiFab v(sol.boxArray(), ncomp, nghost);
    MultiFab t(sol.boxArray(), ncomp, nghost);

    if (verbose && false)
    {
        std::cout << "eps_rel = "       << eps_rel         << std::endl;
        std::cout << "eps_abs = "       << eps_abs         << std::endl;
        std::cout << "lp.norm = "       << Lp.norm(0, lev) << std::endl;
        std::cout << "sol.norm_inf = " << norm_inf(sol)   << std::endl;
        std::cout << "rhs.norm_inf = " << norm_inf(rhs)   << std::endl;
    }

    sorig.copy(sol);
    Lp.residual(r, rhs, sorig, lev, bc_mode);
    rh.copy(r);
    sol.setVal(0.0);
    const LinOp::BC_Mode temp_bc_mode=LinOp::Homogeneous_BC;

    Real rnorm = norm_inf(r);
    const Real rnorm0 = rnorm;

    const Real Lp_norm = Lp.norm(0, lev);
    const Real rh_norm =   rnorm0;
    Real sol_norm = 0.0;
  
    if (verbose > 0 && ParallelDescriptor::IOProcessor())
    {
        Spacer(std::cout, lev);
        std::cout << "CGSolver_bicgstab: Initial error (error0) =        " << rnorm0 << '\n';
    }
    int ret = 0;			// will return this value if all goes well
    Real rho_1 = 0, alpha = 0, omega = 0;
    int nit = 1;
    if ( rnorm == 0.0 || rnorm < eps_rel*(Lp_norm*sol_norm + rh_norm ) || rnorm < eps_abs )
    {
      if (verbose > 0 && ParallelDescriptor::IOProcessor())
	{
	  Spacer(std::cout, lev);
	  std::cout << "CGSolver_bicgstab: niter = 0,"
		    << ", rnorm = " << rnorm 
		    << ", eps_rel*(Lp_norm*sol_norm + rh_norm )" <<  eps_rel*(Lp_norm*sol_norm + rh_norm ) 
		    << ", eps_abs = " << eps_abs << std::endl;
	}
        return 0;
    }
    for (; nit <= maxiter; ++nit)
    {
        Real rho = dotxy(rh, r);
        if ( rho == 0 ) 
	{
            ret = 1;
            break;
	}

        if ( nit == 1 ) {
            p.copy(r);
        } else {
            Real beta = (rho/rho_1)*(alpha/omega);
            sxay(p, p, -omega, v);
            sxay(p, r, beta, p);
        }

        if ( use_mg_precond ) {
            ph.setVal(0.0);
            mg_precond->solve(ph, p, eps_rel, eps_abs, temp_bc_mode);
        } else {
            ph.copy(p);
        }

        Lp.apply(v, ph, lev, temp_bc_mode);

        if ( Real rhTv = dotxy(rh, v) )
	{
            alpha = rho/rhTv;
	}
        else
	{
            ret = 2;
            break;
	}

        sxay(sol, sol, alpha, ph);
        sxay(s, r, -alpha, v);
        rnorm = norm_inf(s);

        if (ParallelDescriptor::IOProcessor())
        {
            if (verbose > 1 ||
                (((eps_rel > 0. && rnorm < eps_rel*rnorm0) ||
                  (eps_abs > 0. && rnorm < eps_abs)) && verbose))
            {
                Spacer(std::cout, lev);
                std::cout << "CGSolver_bicgstab: Half Iter "
                          << std::setw(11) << nit
                          << " rel. err. "
//                        << rnorm/(Lp_norm*sol_norm+rh_norm) << '\n';
                          << rnorm/(rh_norm) << '\n';
            }
        }

#ifndef CG_USE_OLD_CONVERGENCE_CRITERIA
        sol_norm = norm_inf(sol);
        if ( rnorm < eps_rel*(Lp_norm*sol_norm + rh_norm ) || rnorm < eps_abs )
	{
            break;
	}
#else
        if ( rnorm < eps_rel*rnorm0 || rnorm < eps_abs )
	{
            break;
	}
#endif

        if ( use_mg_precond )
        {
            sh.setVal(0.0);
            mg_precond->solve(sh, s, eps_rel, eps_abs, temp_bc_mode);
        }
        else
        {
            sh.copy(s);
        }
        Lp.apply(t, sh, lev, temp_bc_mode);
        if ( Real tTt = dotxy(t,t) )
	{
            omega = dotxy(t,s)/tTt;
	}
        else
	{
            ret = 3;
            break;
	}
        sxay(sol, sol, omega, sh);
        sxay(r, s, -omega, t);
        rnorm = norm_inf(r);

        if (ParallelDescriptor::IOProcessor())
        {
            if (verbose > 1 ||
                (((eps_rel > 0. && rnorm < eps_rel*rnorm0) ||
                  (eps_abs > 0. && rnorm < eps_abs)) && verbose))
            {
                Spacer(std::cout, lev);
                std::cout << "CGSolver_bicgstab: Iteration "
                          << std::setw(11) << nit
                          << " rel. err. "
//                        << rnorm/(Lp_norm*sol_norm+rh_norm) << '\n';
                          << rnorm/(rh_norm) << '\n';
            }
        }

#ifndef CG_USE_OLD_CONVERGENCE_CRITERIA
        sol_norm = norm_inf(sol);
        if ( rnorm < eps_rel*(Lp_norm*sol_norm + rh_norm ) || rnorm < eps_abs )
	{
            break;
	}
#else
        if ( rnorm < eps_rel*rnorm0 || rnorm < eps_abs )
	{
            break;
	}
#endif
        if ( omega == 0 )
	{
            ret = 4;
            break;
	}

        rho_1 = rho;
    }

    if (ParallelDescriptor::IOProcessor())
    {
        if (verbose > 0 ||
            (((eps_rel > 0. && rnorm < eps_rel*rnorm0) ||
              (eps_abs > 0. && rnorm < eps_abs)) && verbose))
	{
            Spacer(std::cout, lev);
            std::cout << "CGSolver_bicgstab: Final: Iteration "
                      << std::setw(4) << nit
                      << " rel. err. "
//                    << rnorm/(Lp_norm*sol_norm+rh_norm) << '\n';
                      << rnorm/(rh_norm) << '\n';
	}
    }
#ifndef CG_USE_OLD_CONVERGENCE_CRITERIA
    if ( ret == 0 && rnorm > eps_rel*(Lp_norm*sol_norm + rh_norm ) && rnorm > eps_abs )
    {
      if ( ParallelDescriptor::IOProcessor() )
	{
	  BoxLib::Warning("CGSolver_bicgstab:: failed to converge!");
	}
      ret = 8;
    }
#else
    if ( ret == 0 && rnorm > eps_rel*rnorm0 && rnorm > eps_abs)
    {
      if ( ParallelDescriptor::IOProcessor() )
	{
	  BoxLib::Warning("CGSolver_bicgstab:: failed to converge!");
	}
      ret = 8;
    }
#endif

    if ( ( ret == 0 || ret == 8 ) && (rnorm < rh_norm) )
    {
      sol.plus(sorig, 0, 1, 0);
    } 
    else 
    {
      sol.setVal(0.0);
      sol.plus(sorig, 0, 1, 0);
    }
    return ret;
}

int
CGSolver::solve_cg (MultiFab&       sol,
		    const MultiFab& rhs,
		    Real            eps_rel,
		    Real            eps_abs,
		    LinOp::BC_Mode  bc_mode)
{
    BL_PROFILE(BL_PROFILE_THIS_NAME() + "::solve_cg()");

    const int nghost = 1;
    const int ncomp = sol.nComp();
    BL_ASSERT(sol.boxArray() == Lp.boxArray(lev));
    BL_ASSERT(rhs.boxArray() == Lp.boxArray(lev));
    BL_ASSERT(ncomp == 1 );

    MultiFab sorig(sol.boxArray(), ncomp, nghost);
    MultiFab r(sol.boxArray(), ncomp, nghost);
    MultiFab z(sol.boxArray(), ncomp, nghost);
    MultiFab q(sol.boxArray(), ncomp, nghost);
    MultiFab p(sol.boxArray(), ncomp, nghost);

    sorig.copy(sol);
    Lp.residual(r, rhs, sorig, lev, bc_mode);
    sol.setVal(0.0);
    const LinOp::BC_Mode temp_bc_mode=LinOp::Homogeneous_BC;

    Real rnorm  = norm_inf(r);
    const Real rnorm0 = rnorm;
    Real minrnorm = rnorm;

    if (verbose > 0 && ParallelDescriptor::IOProcessor())
    {
        Spacer(std::cout, lev);
        std::cout << "              CG: Initial error :        " << rnorm0 << '\n';
    }

    const Real Lp_norm = Lp.norm(0, lev);
    const Real rh_norm =   rnorm0;
    Real sol_norm = 0.0;
    int ret = 0;			// will return this value if all goes well
    Real rho_1 = 0;
    int nit = 1;
    if ( rnorm == 0.0 || rnorm < eps_rel*(Lp_norm*sol_norm + rh_norm ) || rnorm < eps_abs )
    {
      if (verbose > 0 && ParallelDescriptor::IOProcessor())
	{
	  Spacer(std::cout, lev);
	  std::cout << "       CG: niter = 0,"
  		    << ", rnorm = " << rnorm 
		    << ", eps_rel*(Lp_norm*sol_norm + rh_norm )" <<  eps_rel*(Lp_norm*sol_norm + rh_norm ) 
		    << ", eps_abs = " << eps_abs << std::endl;
	}
        return 0;
    }
    for (; nit <= maxiter; ++nit)
    {
        if (use_mg_precond)
        {
            z.setVal(0.);
            mg_precond->solve(z, r, eps_rel, eps_abs, temp_bc_mode);
        }
        else
        {
            z.copy(r);
        }

        Real rho = dotxy(z,r);
        if (nit == 1)
        {
            p.copy(z);
        }
        else
        {
            Real beta = rho/rho_1;
            sxay(p, z, beta, p);
        }

        Lp.apply(q, p, lev, temp_bc_mode);
        Real alpha;
        if ( Real pw = dotxy(p, q) )
	{
            alpha = rho/pw;
	}
        else
	{
            ret = 1;
            break;
	}
        
        if (ParallelDescriptor::IOProcessor() && verbose > 2)
        {
            Spacer(std::cout, lev);
            std::cout << "CGSolver_cg:"
                      << " nit " << nit
                      << " rho " << rho
                      << " alpha " << alpha << '\n';
        }
        sxay(sol, sol, alpha, p);
        sxay(  r,   r,-alpha, q);
        rnorm = norm_inf(r);
        sol_norm = norm_inf(sol);

        if (ParallelDescriptor::IOProcessor())
        {
            if (verbose > 1 ||
                (((eps_rel > 0. && rnorm < eps_rel*rnorm0) ||
                  (eps_abs > 0. && rnorm < eps_abs)) && verbose))
            {
                Spacer(std::cout, lev);
                std::cout << "       CG:       Iteration"
                          << std::setw(4) << nit
//                        << " norm "
//                        << rnorm 
                          << " rel. err. "
//                        << rnorm/(Lp_norm*sol_norm+rh_norm) << '\n';
                          << rnorm/(rh_norm) << '\n';
            }
        }

#ifndef CG_USE_OLD_CONVERGENCE_CRITERIA
        if ( rnorm < eps_rel*(Lp_norm*sol_norm + rh_norm) || rnorm < eps_abs )
	{
            break;
	}
#else
        if ( rnorm < eps_rel*rnorm0 || rnorm < eps_abs )
	{
            break;
	}
#endif
      
        if ( rnorm > def_unstable_criterion*minrnorm )
	{
            ret = 2;
            break;
	}
        else if ( rnorm < minrnorm )
	{
            minrnorm = rnorm;
	}

        rho_1 = rho;
    }
    
    if (ParallelDescriptor::IOProcessor())
    {
        if (verbose > 0 ||
            (((eps_rel > 0. && rnorm < eps_rel*rnorm0) ||
              (eps_abs > 0. && rnorm < eps_abs)) && verbose))
	{
            Spacer(std::cout, lev);
            std::cout << "       CG: Final Iteration"
                      << std::setw(4) << nit
//                    << " norm "
//                    << rnorm 
                      << " rel. err. "
//                    << rnorm/(Lp_norm*sol_norm+rh_norm) << '\n';
                      << rnorm/(rh_norm) << '\n';
	}
    }
#ifndef CG_USE_OLD_CONVERGENCE_CRITERIA
    if ( ret == 0 && rnorm > eps_rel*(Lp_norm*sol_norm + rh_norm) && rnorm > eps_abs )
    {
      if ( ParallelDescriptor::IOProcessor() )
	{
	  BoxLib::Warning("CGSolver_cg:: failed to converge!");
	}
      ret = 8;
    }
#else
    if ( ret == 0 &&  rnorm > eps_rel*rnorm0 && rnorm > eps_abs )
    {
      if ( ParallelDescriptor::IOProcessor() )
	{
	  BoxLib::Warning("CGSolver_cg:: failed to converge!");
	}
      ret = 8;
    }
#endif

    if ( ( ret == 0 || ret == 8 ) && (rnorm < rh_norm) )
    {
      sol.plus(sorig, 0, 1, 0);
    } 
    else 
    {
      sol.setVal(0.0);
      sol.plus(sorig, 0, 1, 0);
    }

    return ret;
}

