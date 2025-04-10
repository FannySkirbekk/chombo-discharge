/* chombo-discharge
 * Copyright © 2021 SINTEF Energy Research.
 * Please refer to Copyright.txt and LICENSE in the chombo-discharge root directory.
 */

/*!
  @file   CD_ItoKMCPhysicsImplem.H 
  @brief  Implementation of CD_ItoKMCPhysics.H
  @author Robert Marskar
*/

#ifndef CD_ItoKMCPhysicsImplem_H
#define CD_ItoKMCPhysicsImplem_H

// Chombo includes
#include <ParmParse.H>

// Our includes
#include <CD_ItoKMCPhysics.H>
#include <CD_ParticleManagement.H>
#include <CD_Random.H>
#include <CD_Units.H>
#include <CD_NamespaceHeader.H>

using namespace Physics::ItoKMC;

inline ItoKMCPhysics::ItoKMCPhysics() noexcept
{
  CH_TIME("ItoKMCPhysics::ItoKMCPhysics");

  m_className = "ItoKMCPhysics";

  m_kmcReactions.clear();
  m_photoReactions.clear();

  // Some default settings in case user forgets to call the parsing algorithms.
  m_isDefined         = false;
  m_debug             = true;
  m_hasKMCSolver      = false;
  m_maxNewParticles   = 32;
  m_maxNewPhotons     = 32;
  m_Ncrit             = 5;
  m_eps               = 0.1;
  m_NSSA              = 10;
  m_SSAlim            = 5.0;
  m_algorithm         = Algorithm::TauPlain;
  m_particlePlacement = ParticlePlacement::Random;

  // Development code for switching to centroid for secondary emission. Will be removed.
#if 0
  bool      useCentroid = false;
  ParmParse pp("ItoKMCPhysics");
  pp.query("use_centroid", useCentroid);
  if (useCentroid) {
    m_particlePlacement = ParticlePlacement::Centroid;
  }
#endif
}

inline ItoKMCPhysics::~ItoKMCPhysics() noexcept
{
  CH_TIME("ItoKMCPhysics::~ItoKMCPhysics");
}

inline void
ItoKMCPhysics::define() noexcept
{
  CH_TIME("ItoKMCPhysics::define");

  this->defineSpeciesMap();
  this->definePhotoPathways();

  // Safety hook -- make sure no one defines reactions using an out-of-range species index.
#ifndef NDEBUG
  for (const auto& R : m_kmcReactions) {
    const auto& lhsReactants = R.getReactants();
    const auto& rhsReactants = R.getReactiveProducts();
    const auto& rhsPhotons   = R.getNonReactiveProducts();

    for (const auto& r : lhsReactants) {
      CH_assert(r < m_itoSpecies.size() + m_cdrSpecies.size());
    }
    for (const auto& r : rhsReactants) {
      CH_assert(r < m_itoSpecies.size() + m_cdrSpecies.size());
    }
    for (const auto& r : rhsPhotons) {
      CH_assert(r < m_rtSpecies.size());
    }
  }
#endif

  m_isDefined = true;
}

inline void
ItoKMCPhysics::defineSpeciesMap() noexcept
{
  CH_TIME("ItoKMCPhysics::defineSpeciesMap");

  const int numItoSpecies = this->getNumItoSpecies();
  const int numCdrSpecies = this->getNumCdrSpecies();

  int species = 0;
  for (int i = 0; i < numItoSpecies; i++, species++) {
    m_speciesMap.emplace(species, std::make_pair(SpeciesType::Ito, i));
  }

  for (int i = 0; i < numCdrSpecies; i++, species++) {
    m_speciesMap.emplace(species, std::make_pair(SpeciesType::CDR, i));
  }
}

inline void
ItoKMCPhysics::defineKMC() const noexcept
{
  CH_TIME("ItoKMCPhysics::defineKMC");

  CH_assert(!m_hasKMCSolver);

  // Deep copy of reaction rates
  m_kmcReactionsThreadLocal.resize(0);
  for (const auto& r : m_kmcReactions) {
    m_kmcReactionsThreadLocal.emplace_back(std::make_shared<const KMCReaction>(r));
  }

  m_kmcSolver.define(m_kmcReactionsThreadLocal);
  m_kmcSolver.setSolverParameters(m_Ncrit, m_NSSA, m_eps, m_SSAlim);
  m_kmcState.define(m_itoSpecies.size() + m_cdrSpecies.size(), m_rtSpecies.size());

  m_hasKMCSolver = true;
}

inline void
ItoKMCPhysics::killKMC() const noexcept
{
  CH_TIME("ItoKMCPhysics::defineKMC");

  CH_assert(m_hasKMCSolver);

  m_kmcReactionsThreadLocal.resize(0);
  m_kmcSolver.define(m_kmcReactionsThreadLocal);
  m_kmcState.define(0, 0);

  m_hasKMCSolver = false;
}

inline void
ItoKMCPhysics::definePhotoPathways() noexcept
{
  CH_TIME("ItoKMCPhysics::definePhotoPathways");

  // Build a temporary list of pathways. I.e. restructure the list of reactions
  //
  // Y1 -> A
  // Y1 -> B
  // Y2 -> C
  // Y2 -> D
  //
  // into separate lists for Y1, Y2, ....
  //
  std::map<int, std::vector<std::pair<int, Real>>> pathways;

  for (int i = 0; i < m_photoReactions.size(); i++) {
    const ItoKMCPhotoReaction& r = m_photoReactions[i];

    const size_t& src        = r.getSourcePhoton();
    const Real    efficiency = r.getEfficiency();

    pathways[src].emplace_back(std::make_pair(i, efficiency));
  }

  // Go through the temporary pathways list and compute the relative efficiencies of one of the
  // photons triggering a reaction. The relative efficiencies are given by
  //
  // p(i) = R(i)/sum_j R(j).
  //
  for (const auto& p : pathways) {
    const int                               photoSpecies             = p.first;
    const std::vector<std::pair<int, Real>> reactionsAndEfficiencies = p.second;

    std::map<int, int> localToGlobalMap;
    std::list<double>  efficiencies;
    double             sumEfficiencies = 0.0;

    for (int i = 0; i < reactionsAndEfficiencies.size(); i++) {
      sumEfficiencies += (double)reactionsAndEfficiencies[i].second;
    }

    for (int i = 0; i < reactionsAndEfficiencies.size(); i++) {
      localToGlobalMap.emplace(i, reactionsAndEfficiencies[i].first);
      efficiencies.emplace_back((double)reactionsAndEfficiencies[i].second / sumEfficiencies);
    }

    std::discrete_distribution<int> distribution(efficiencies.begin(), efficiencies.end());

    m_photoPathways.insert(std::make_pair((int)photoSpecies, std::make_pair(distribution, localToGlobalMap)));
  }
}

inline const std::map<int, std::pair<SpeciesType, int>>&
ItoKMCPhysics::getSpeciesMap() const noexcept
{
  CH_TIME("ItoKMCPhysics::getSpeciesMap");

  return m_speciesMap;
}

inline Real
ItoKMCPhysics::computeDt(const RealVect a_E, const RealVect a_pos, const Vector<FPR> a_numParticles) const noexcept
{
  CH_TIME("ItoKMCPhysics::computeDt");

  CH_assert(m_isDefined);

  return std::numeric_limits<Real>::max();
}

inline void
ItoKMCPhysics::parseRuntimeOptions() noexcept
{
  CH_TIME("ItoKMCPhysics::parseRuntimeOptions");

  this->parsePPC();
  this->parseDebug();
  this->parseAlgorithm();
}

inline void
ItoKMCPhysics::parsePPC() noexcept
{
  CH_TIME("ItoKMCPhysics::parsePPC");

  ParmParse pp(m_className.c_str());

  pp.get("max_new_particles", m_maxNewParticles);
  pp.get("max_new_photons", m_maxNewPhotons);
}

inline void
ItoKMCPhysics::parseDebug() noexcept
{
  CH_TIME("ItoKMCPhysics::parseDebug");

  ParmParse pp(m_className.c_str());

  pp.get("debug", m_debug);
}

inline void
ItoKMCPhysics::parseAlgorithm() noexcept
{
  CH_TIME("ItoKMCPhysics::parseAlgorithm");

  ParmParse pp(m_className.c_str());

  std::string str;

  pp.get("algorithm", str);
  pp.get("Ncrit", m_Ncrit);
  pp.get("NSSA", m_NSSA);
  pp.get("prop_eps", m_eps);
  pp.get("SSAlim", m_SSAlim);

  if (str == "ssa") {
    m_algorithm = Algorithm::SSA;
  }
  else if (str == "tau_plain") {
    m_algorithm = Algorithm::TauPlain;
  }
  else if (str == "tau_midpoint") {
    m_algorithm = Algorithm::TauMidpoint;
  }
  else if (str == "tau_prc") {
    m_algorithm = Algorithm::TauPRC;
  }
  else if (str == "hybrid_plain") {
    m_algorithm = Algorithm::HybridPlain;
  }
  else if (str == "hybrid_midpoint") {
    m_algorithm = Algorithm::HybridMidpoint;
  }
  else if (str == "hybrid_prc") {
    m_algorithm = Algorithm::HybridPRC;
  }
  else {
    MayDay::Error("ItoKMCPhysics::parseAlgorithm - unknown algorithm requested");
  }
}

inline const Vector<RefCountedPtr<ItoSpecies>>&
ItoKMCPhysics::getItoSpecies() const
{
  return m_itoSpecies;
}

inline const Vector<RefCountedPtr<CdrSpecies>>&
ItoKMCPhysics::getCdrSpecies() const
{
  return m_cdrSpecies;
}

inline const Vector<RefCountedPtr<RtSpecies>>&
ItoKMCPhysics::getRtSpecies() const
{
  return m_rtSpecies;
}

inline int
ItoKMCPhysics::getNumItoSpecies() const
{
  return m_itoSpecies.size();
}

inline int
ItoKMCPhysics::getNumCdrSpecies() const
{
  return m_cdrSpecies.size();
}

inline int
ItoKMCPhysics::getNumPlasmaSpecies() const
{
  return m_itoSpecies.size() + m_cdrSpecies.size();
}

inline int
ItoKMCPhysics::getNumPhotonSpecies() const
{
  return m_rtSpecies.size();
}

inline Real
ItoKMCPhysics::initialSigma(const Real a_time, const RealVect a_pos) const
{
  return 0.0;
}

inline void
ItoKMCPhysics::advanceKMC(Vector<FPR>&            a_numParticles,
                          Vector<FPR>&            a_numNewPhotons,
                          const Vector<Real>&     a_phi,
                          const Vector<RealVect>& a_gradPhi,
                          const Real              a_dt,
                          const RealVect          a_E,
                          const RealVect          a_pos,
                          const Real              a_dx,
                          const Real              a_kappa) const
{
  CH_TIME("ItoKMCPhysics::advanceKMC");

  // Note: This is called PER GRID CELL, i.e. within OpenMP parallel regions. For this reason the KMC solver
  //       must be defined through defineKMC() (which must be later killed).
  CH_assert(m_isDefined);
  CH_assert(m_hasKMCSolver);

  std::vector<FPR>& kmcParticles = m_kmcState.getReactiveState();
  std::vector<FPR>& kmcPhotons   = m_kmcState.getNonReactiveState();

  for (size_t i = 0; i < a_numParticles.size(); i++) {
    kmcParticles[i] = a_numParticles[i];
  }

  for (auto& p : kmcPhotons) {
    p = 0LL;
  }

  // Update the reaction rates to be used by the KMC solver.
  this->updateReactionRates(m_kmcReactionsThreadLocal, a_E, a_pos, a_phi, a_gradPhi, a_dx, a_kappa);

  // Run the KMC solver.
  switch (m_algorithm) {
  case Algorithm::SSA: {
    m_kmcSolver.advanceSSA(m_kmcState, a_dt);

    break;
  }
  case Algorithm::TauPlain: {
    m_kmcSolver.advanceTauPlain(m_kmcState, a_dt);

    break;
  }
  case Algorithm::TauMidpoint: {
    m_kmcSolver.advanceTauMidpoint(m_kmcState, a_dt);

    break;
  }
  case Algorithm::TauPRC: {
    m_kmcSolver.advanceTauPRC(m_kmcState, a_dt);

    break;
  }
  case Algorithm::HybridPlain: {
    m_kmcSolver.advanceHybrid(m_kmcState, a_dt, KMCLeapPropagator::TauPlain);

    break;
  }
  case Algorithm::HybridMidpoint: {
    m_kmcSolver.advanceHybrid(m_kmcState, a_dt, KMCLeapPropagator::TauMidpoint);

    break;
  }
  case Algorithm::HybridPRC: {
    m_kmcSolver.advanceHybrid(m_kmcState, a_dt, KMCLeapPropagator::TauPRC);

    break;
  }
  default: {
    MayDay::Error("ItoKMCPhysics::advanceKMC - logic bust");
  }
  }

  // Put KMC back into ItoKMC
  for (size_t i = 0; i < a_numParticles.size(); i++) {
    a_numParticles[i] = (FPR)kmcParticles[i];
  }
  for (size_t i = 0; i < a_numNewPhotons.size(); i++) {
    a_numNewPhotons[i] = (FPR)kmcPhotons[i];
  }
}

inline void
ItoKMCPhysics::reconcileParticles(Vector<List<ItoParticle>*>& a_particles,
                                  const Vector<FPR>&          a_newNumParticles,
                                  const Vector<FPR>&          a_oldNumParticles,
                                  const RealVect              a_cellPos,
                                  const RealVect              a_centroidPos,
                                  const RealVect              a_lo,
                                  const RealVect              a_hi,
                                  const RealVect              a_bndryCentroid,
                                  const RealVect              a_bndryNormal,
                                  const Real                  a_dx,
                                  const Real                  a_kappa) const noexcept
{
  CH_TIME("ItoKMCPhysics::reconcileParticles(Vector<List<ItoParticle>*>, ...)");

  CH_assert(m_isDefined);
  CH_assert(a_particles.size() == a_newNumParticles.size());
  CH_assert(a_oldNumParticles.size() == a_newNumParticles.size());

  if (m_debug) {
    for (int i = 0; i < a_particles.size(); i++) {
      const FPR& numNew = a_newNumParticles[i];
      const FPR& numOld = a_oldNumParticles[i];

      if (numNew < (FPR)0) {
        MayDay::Warning("ItoKMCPhysics::reconcileParticles - new number of particles is < 0 (overflow issue?)");
      }
      else if ((long long)numNew < 0LL) {
        MayDay::Warning("ItoKMCPhysics::reconcileParticles - integer overflow!");
      }

      if (numOld < 0) {
        MayDay::Warning("ItoKMCPhysics::reconcileParticles - old number of particles is < 0");
      }
      else if ((long long)numOld < 0LL) {
        MayDay::Warning("ItoKMCPhysics::reconcileParticles - integer overflow for old particles!");
      }
    }
  }

  for (int i = 0; i < a_particles.size(); i++) {
    const long long diff = (long long)(a_newNumParticles[i] - a_oldNumParticles[i]);

    if (diff > 0LL) {
      // Adding particles, which is fairly simple. Just choose weights for the particles and go.
      const std::vector<long long> particleWeights = ParticleManagement::partitionParticleWeights(
        diff,
        (long long)m_maxNewParticles);

      for (const auto& w : particleWeights) {
        RealVect x = RealVect::Zero;

        // Figure out where to place the particles.
        switch (m_particlePlacement) {
        case ParticlePlacement::Random: {
          x = Random::randomPosition(a_cellPos, a_lo, a_hi, a_bndryCentroid, a_bndryNormal, a_dx, a_kappa);

          break;
        }
        case ParticlePlacement::Centroid: {
          x = a_cellPos + a_centroidPos * a_dx;

          break;
        }
        default: {
          MayDay::Error("ItoKMCPhysics::reconcileParticles - logic bust");

          break;
        }
        }

        a_particles[i]->add(ItoParticle(1.0 * w, x));
      }
    }
    else if (diff < 0LL) {
      // Removing particles is a bit more difficult because we need to manipulate weights.
      this->removeParticles(*a_particles[i], -diff);
    }
  }
}

inline void
ItoKMCPhysics::removeParticles(List<ItoParticle>& a_particles, const long long a_numParticlesToRemove) const
{
  CH_TIME("ItoKMCPhysics::removeParticles");

  constexpr long long zero = 0LL;

  CH_assert(m_isDefined);
  CH_assert(a_numParticlesToRemove >= zero);

  // Quick lambda for getting total particle weight. Used for debugging.
  auto getTotalWeight = [&]() -> long long {
    long long W = zero;

    for (ListIterator<ItoParticle> lit(a_particles); lit.ok(); ++lit) {
      W += (long long)lit().weight();

      if (lit().weight() < 1.0) {
        MayDay::Error("ItoKMCPhysics::removeParticles -- bad particle mass!");
      }
    }

    return W;
  };

  if (a_numParticlesToRemove > zero) {

    // For debugging only.
    long long totalWeightBefore = 0;
    long long totalWeightAfter  = 0;

    // Debug hook, compute the total particle weight before we start removing weights.
    if (m_debug) {
      totalWeightBefore = getTotalWeight();

      if (totalWeightBefore < a_numParticlesToRemove) {
        MayDay::Error("ItoKMCPhysics::removeParticles: logic bust (trying to remove too many particles)");
      }
    }

    // Remove physical particles.
    ParticleManagement::removePhysicalParticles(a_particles, a_numParticlesToRemove);

    // Remove particles with too low weight.
    ParticleManagement::deleteParticles(a_particles, std::numeric_limits<Real>::min());

    // Debug hook, make sure that particle weights are > 0 AND we've removed the desired
    // particle weight.
    if (m_debug) {
      totalWeightAfter = getTotalWeight();

      const long long errDiff = std::abs(totalWeightBefore - totalWeightAfter) - a_numParticlesToRemove;
      if (std::abs(errDiff) != zero) {

        pout() << "ItoKMCPhysics::removeParticles: Total weight before = " << totalWeightBefore << endl;
        pout() << "ItoKMCPhysics::removeParticles: Total weight after  = " << totalWeightAfter << endl;
        pout() << "ItoKMCPhysics::removeParticles: Should have removed = " << a_numParticlesToRemove << endl;
        pout() << "ItoKMCPhysics::removeParticles: Error               = " << errDiff << endl;

        MayDay::Abort("ItoKMCPhysics::removeParticles - incorrect mass removed");
      }
    }
  }
}

inline void
ItoKMCPhysics::reconcilePhotons(Vector<List<Photon>*>& a_newPhotons,
                                const Vector<FPR>&     a_numNewPhotons,
                                const RealVect         a_cellPos,
                                const RealVect         a_centroidPos,
                                const RealVect         a_lo,
                                const RealVect         a_hi,
                                const RealVect         a_bndryCentroid,
                                const RealVect         a_bndryNormal,
                                const Real             a_dx,
                                const Real             a_kappa) const noexcept
{
  CH_TIME("ItoKMCPhysics::reconcilePhotons");

  CH_assert(m_isDefined);

  for (int i = 0; i < a_newPhotons.size(); i++) {
    if (a_numNewPhotons[i] > 0LL) {

      const std::vector<long long> photonWeights = ParticleManagement::partitionParticleWeights(
        (long long)a_numNewPhotons[i],
        (long long)m_maxNewPhotons);

      for (const auto& w : photonWeights) {
        const RealVect x = Random::randomPosition(a_cellPos, a_lo, a_hi, a_bndryCentroid, a_bndryNormal, a_dx, a_kappa);
        const RealVect v = Units::c * Random::getDirection();

        a_newPhotons[i]->add(Photon(x, v, m_rtSpecies[i]->getAbsorptionCoefficient(x), 1.0 * w));
      }
    }
  }
}

inline void
ItoKMCPhysics::reconcilePhotoionization(Vector<List<ItoParticle>*>&   a_itoParticles,
                                        Vector<List<PointParticle>*>& a_cdrParticles,
                                        const Vector<List<Photon>*>&  a_absorbedPhotons) const noexcept
{
  CH_TIME("ItoKMCPhysics::reconcilePhotoionization");

  CH_assert(m_isDefined);
  CH_assert(a_itoParticles.size() == m_itoSpecies.size());
  CH_assert(a_cdrParticles.size() == m_cdrSpecies.size());

  for (int i = 0; i < a_absorbedPhotons.size(); i++) {
    if (m_photoPathways.find(i) != m_photoPathways.end()) {
      std::discrete_distribution<int> d                = m_photoPathways.at(i).first;
      const std::map<int, int>&       localToGlobalMap = m_photoPathways.at(i).second;

      for (ListIterator<Photon> lit(*a_absorbedPhotons[i]); lit.ok(); ++lit) {
        const RealVect x = lit().position();
        const Real     w = lit().weight();

        // Determine the photo-reaction type.
        const int localReaction  = Random::get(d);
        const int globalReaction = localToGlobalMap.at(localReaction);

        const ItoKMCPhotoReaction& photoReaction = m_photoReactions[globalReaction];
        const std::list<size_t>&   plasmaTargets = photoReaction.getTargetSpecies();

        for (const auto& t : plasmaTargets) {
          const SpeciesType& type       = m_speciesMap.at(t).first;
          const int&         localIndex = m_speciesMap.at(t).second;

          if (type == SpeciesType::Ito) {
            a_itoParticles[localIndex]->add(ItoParticle(w, x));
          }
          else if (type == SpeciesType::CDR) {
            a_cdrParticles[localIndex]->add(PointParticle(x, w));
          }
          else {
            MayDay::Error("CD_ItoKMCPhysics.H - logic bust in reconcilePhotoionization");
          }
        }
      }
    }
  }
}

#include <CD_NamespaceFooter.H>

#endif
