/* chombo-discharge
 * Copyright © 2022 SINTEF Energy Research.
 * Please refer to Copyright.txt and LICENSE in the chombo-discharge root directory.
 */

/*!
  @file   CD_TracerParticle.H
  @brief  Declaration of a tracer particle class.
  @author Robert Marskar
*/

#ifndef CD_TracerParticle_H
#define CD_TracerParticle_H

// Std includes
#include <string>
#include <typeinfo>
#include <cstdio>
#include <cmath>

// Our includes
#include <CD_GenericParticle.H>
#include <CD_NamespaceHeader.H>

/*!
  @brief A tracer particle class. This is templated for holding extra storage (useful for kernels). 
  @details The template parameters M and N determine extra storage allocated to the particle. M determines
  the number of allocated scalars (Reals) and N determines the number of allocated vectors (RealVects). These
  quantities are communicated when remapping particles. 
*/
template <size_t M, size_t N>
class TracerParticle : public GenericParticle<M, N>
{
public:
  /*!
    @brief Default constructor -- initializes everything to zero. 
  */
  inline TracerParticle();

  /*!
    @brief Copy constructor. Copies all fields.
    @param[in] a_other Other particle. 
  */
  inline TracerParticle(const TracerParticle<M, N>& a_other);

  /*!
    @brief Destructor (deallocates runtime memory storage)
  */
  inline virtual ~TracerParticle();

  /*!
    @brief Get the particle "weight"
    @return m_weight
  */
  inline Real&
  weight();

  /*!
    @brief Get the particle "weight"
    @return m_weight
  */
  inline const Real&
  weight() const;

  /*!
    @brief Get the particle velocity. 
    @return m_velocity
  */
  inline RealVect&
  velocity();

  /*!
    @brief Get the particle velocity.
    @return m_velocity
  */
  inline const RealVect&
  velocity() const;

  /*!
    @brief Returns the size, in number of bytes, of a flat representation of the data in this object.
  */
  inline virtual int
  size() const override;

  /*!
    @brief Write a linear binary representation of the internal data. Assumes that
    sufficient memory for the buffer has already been allocated by the caller. 
    @param[in] a_buffer Pointer to memory block
  */
  inline virtual void
  linearOut(void* a_buffer) const override;

  /*!
    @brief Read a linear binary representation of the internal data. Assumes that the buffer has the correct data.
    @param[in] a_buffer Pointer to memory block
  */
  inline virtual void
  linearIn(void* a_buffer) override;

  /*!
    @brief Get one of the scalars. 
    @details Template parameter is the position in the m_scalars array. This is templated so that compilers may throw
    compile-time errors if trying to fetch elements out of range. 
    @return m_scalars[K]
  */
  template <size_t K>
  inline Real&
  real();

  /*!
    @brief Get one of the scalars. 
    @details Template parameter is the position in the m_scalars array. This is templated so that compilers may throw
    compile-time errors if trying to fetch elements out of range. 
    @return m_scalars[K]
  */
  template <size_t K>
  inline const Real&
  real() const;

  /*!
    @brief Get one of the RealVects.
    @details Template parameter is the position in the m_vectors array. This is templated so that compilers may throw
    compile-time errors if trying to fetch elements out of range. 
    @return m_vectors[K]
  */
  template <size_t K>
  inline RealVect&
  vect();

  /*!
    @brief Get one of the RealVects.
    @details Template parameter is the position in the m_vectors array. This is templated so that compilers may throw
    compile-time errors if trying to fetch elements out of range. 
    @return m_vectors[K]
  */
  template <size_t K>
  inline const RealVect&
  vect() const;

protected:
  /*!
    @brief Particle "weight".
  */
  Real m_weight;

  /*!
    @brief Particle velocity
  */
  RealVect m_velocity;
};

/*!
  @brief Particle printing function
  @param[in] ostr       Output stream
  @param[in] a_particle Particle to output
*/
template <size_t M, size_t N>
inline std::ostream&
operator<<(std::ostream& ostr, const TracerParticle<M, N>& p);

#include <CD_NamespaceFooter.H>

#include <CD_TracerParticleImplem.H>

#endif
