//
// $Id: Utility.cpp,v 1.1 2007-05-24 18:12:02 tdsternberg Exp $
//

#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <iostream>

#include <sys/stat.h>
#include <sys/types.h>
#ifndef WIN32
#include <sys/wait.h>
#endif
#include <errno.h>

#include <BLFort.H>
#include <REAL.H>
#include <BoxLib.H>
#include <Utility.H>
#include <BLassert.H>

#ifdef BL_BGL
#include <ParallelDescriptor.H>
#endif

#ifdef WIN32
#include <direct.h>
#define mkdir(a,b) _mkdir((a))
const char* path_sep_str = "\\";
#else
const char* path_sep_str = "/";
#endif

#ifdef BL_T3E
#include <malloc.h>
#endif

#if !defined(BL_ARCH_CRAY) && !defined(WIN32) && !defined(BL_T3E) && !defined(BL_XT3)

#include <sys/types.h>
#include <sys/times.h>
#ifdef BL_AIX
#undef _XOPEN_SOURCE_EXTENDED
#define _XOPEN_SOURCE_EXTENDED 1
#endif
#include <sys/time.h>
#ifdef BL_AIX
#undef _XOPEN_SOURCE_EXTENDED
#endif
#include <sys/param.h>
#include <unistd.h>

//
// This doesn't seem to be defined on SunOS when using g++.
//
#if defined(__GNUG__) && defined(__sun) && defined(BL_SunOS)
extern "C" int gettimeofday (struct timeval*, struct timezone*);
#endif

double
BoxLib::second (double* t)
{
    struct tms buffer;

    times(&buffer);

    static long CyclesPerSecond = 0;

    if (CyclesPerSecond == 0)
    {
#if defined(_SC_CLK_TCK)
        CyclesPerSecond = sysconf(_SC_CLK_TCK);
        if (CyclesPerSecond == -1)
            BoxLib::Error("second(double*): sysconf() failed");
#elif defined(HZ)
        CyclesPerSecond = HZ;
#else
        CyclesPerSecond = 100;
        BoxLib::Warning("second(): sysconf(): default value of 100 for hz, worry about timings");
#endif
    }

    double dt = (buffer.tms_utime + buffer.tms_stime)/(1.0*CyclesPerSecond);

    if (t != 0)
        *t = dt;

    return dt;
}

static
double
get_initial_wall_clock_time ()
{
    struct timeval tp;

    if (gettimeofday(&tp, 0) != 0)
        BoxLib::Abort("get_time_of_day(): gettimeofday() failed");

    return tp.tv_sec + tp.tv_usec/1000000.0;
}

//
// Attempt to guarantee wsecond() gets initialized on program startup.
//
double BL_Initial_Wall_Clock_Time = get_initial_wall_clock_time();

double
BoxLib::wsecond (double* t)
{
    struct timeval tp;

    gettimeofday(&tp,0);

    double dt = tp.tv_sec + tp.tv_usec/1000000.0 - BL_Initial_Wall_Clock_Time;

    if (t != 0)
        *t = dt;

    return dt;
}

#elif defined(WIN32)

// minimum requirement of WindowsNT
#include <windows.h>

namespace
{
double rate;
bool inited = false;
LONGLONG
get_initial_wall_clock_time()
{
    LARGE_INTEGER li;
    QueryPerformanceFrequency(&li);
    rate = 1.0/li.QuadPart;
    QueryPerformanceCounter(&li);
    inited = true;
    return li.QuadPart;
}
LONGLONG BL_Initial_Wall_Clock_Time = get_initial_wall_clock_time();
}
double
BoxLib::wsecond(double* rslt)
{
    BL_ASSERT( inited );
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    double result = double(li.QuadPart-BL_Initial_Wall_Clock_Time)*rate;
    if ( rslt ) *rslt = result;
    return result;
}

#include <time.h>
double
BoxLib::second (double* r)
{
    static clock_t start = -1;

    clock_t finish = clock();

    if (start == -1)
        start = finish;

    double rr = double(finish - start)/CLOCKS_PER_SEC;

    if (r)
        *r = rr;

    return rr;
}

#elif defined(BL_XT3)

#include <catamount/dclock.h>
#include <unistd.h>

static
double
get_initial_wall_clock_time ()
{
    return dclock();
}

//
// Attempt to guarantee wsecond() gets initialized on program startup.
//
double BL_Initial_Wall_Clock_Time = get_initial_wall_clock_time();

double
BoxLib::wsecond (double* t_)
{
    double t = dclock() - BL_Initial_Wall_Clock_Time;
    if (t_)
        *t_ = t;
    return t;
}

#elif defined(BL_ARCH_CRAY)


#include <unistd.h>

extern "C" double SECOND();
extern "C" double RTC();

double
BoxLib::second (double* t_)
{
    double t = SECOND();
    if (t_)
        *t_ = t;
    return t;
}

static
double
get_initial_wall_clock_time ()
{
    return RTC();
}

//
// Attempt to guarantee wsecond() gets initialized on program startup.
//
double BL_Initial_Wall_Clock_Time = get_initial_wall_clock_time();

double
BoxLib::wsecond (double* t_)
{
    double t = RTC() - BL_Initial_Wall_Clock_Time;
    if (t_)
        *t_ = t;
    return t;
}

#elif defined(BL_T3E)

//#include <intrinsics.h>
#include <unistd.h>
extern "C" long _rtc();

static double BL_Clock_Rate;
extern "C"
{
long IRTC_RATE();
long _irt();
}

static
long
get_initial_wall_clock_time ()
{
    BL_Clock_Rate = IRTC_RATE();
    return _rtc();
}

//
// Attempt to guarantee wsecond() gets initialized on program startup.
//
long BL_Initial_Wall_Clock_Time = get_initial_wall_clock_time();

//
// NOTE: this is returning wall clock time, instead of cpu time.  But on
// the T3E, there is no difference (currently).  If we call second() instead,
// we may be higher overhead.  Think about this one.
//
double
BoxLib::second (double* t_)
{
    double t = (_rtc() - BL_Initial_Wall_Clock_Time)/BL_Clock_Rate;
    if (t_)
        *t_ = t;
    return t;
}

double
BoxLib::wsecond (double* t_)
{
    double t = (_rtc() - BL_Initial_Wall_Clock_Time)/BL_Clock_Rate;
    if (t_)
        *t_ = t;
    return t;
}

#else

#include <time.h>

double
BoxLib::second (double* r)
{
    static clock_t start = -1;

    clock_t finish = clock();

    if (start == -1)
        start = finish;

    double rr = double(finish - start)/CLOCKS_PER_SEC;

    if (r)
        *r = rr;

    return rr;
}

static
time_t
get_initial_wall_clock_time ()
{
    return ::time(0);
}

//
// Attempt to guarantee wsecond() gets initialized on program startup.
//
time_t BL_Initial_Wall_Clock_Time = get_initial_wall_clock_time();

double
BoxLib::wsecond (double* r)
{
    time_t finish;

    time(&finish);

    double rr = double(finish - BL_Initial_Wall_Clock_Time);

    if (r)
        *r = rr;

    return rr;
}

#endif /*!defined(BL_ARCH_CRAY) && !defined(WIN32) && !defined(BL_T3E)*/

void
BoxLib::ResetWallClockTime ()
{
    BL_Initial_Wall_Clock_Time = get_initial_wall_clock_time();
}

//
// Return true if argument is a non-zero length string of digits.
//

bool
BoxLib::is_integer (const char* str)
{
    int len = 0;

    if (str == 0 || (len = strlen(str)) == 0)
        return false;

    for (int i = 0; i < len; i++)
        if (!isdigit(str[i]))
            return false;

    return true;
}

std::string
BoxLib::Concatenate (const std::string& root,
                     int                num)
{
    std::string result = root;
    char buf[32];
    sprintf(buf, "%04d", num);
    result += buf;
    return result;
}

//
// Creates the specified directories.  `path' may be either a full pathname
// or a relative pathname.  It will create all the directories in the
// pathname, if they don't already exist, so that on successful return the
// pathname refers to an existing directory.  Returns true or false
// depending upon whether or not all it was successful.  Also returns
// true if `path' is NULL.  `mode' is the mode passed to mkdir() for
// any directories that must be created.
//
// For example, if it is passed the string "/a/b/c/d/e/f/g", it
// will return successfully when all the directories in the pathname
// exist; i.e. when the full pathname is a valid directory.
//

bool
#ifdef WIN32
BoxLib::UtilCreateDirectory (const std::string& path,
                             int)
#else
BoxLib::UtilCreateDirectory (const std::string& path,
                             mode_t         mode)
#endif
{
    if (path.length() == 0 || path == path_sep_str)
        return true;

    if (strchr(path.c_str(), *path_sep_str) == 0)
    {
        //
        // No slashes in the path.
        //
        return mkdir(path.c_str(),mode) < 0 && errno != EEXIST ? false : true;
    }
    else
    {
        //
        // Make copy of the directory pathname so we can write to it.
        //
        char* dir = new char[path.length() + 1];
        (void) strcpy(dir, path.c_str());

        char* slash = strchr(dir, *path_sep_str);

        if (dir[0] == *path_sep_str)
        {
            //
            // Got a full pathname.
            //
            do
            {
                if (*(slash+1) == 0)
                    break;
                if ((slash = strchr(slash+1, *path_sep_str)) != 0)
                    *slash = 0;
                if (mkdir(dir, mode) < 0 && errno != EEXIST)
                    return false;
                if (slash)
                    *slash = *path_sep_str;
            } while (slash);
        }
        else
        {
            //
            // Got a relative pathname.
            //
            do
            {
                *slash = 0;
                if (mkdir(dir, mode) < 0 && errno != EEXIST)
                    return false;
                *slash = *path_sep_str;
            } while ((slash = strchr(slash+1, *path_sep_str)) != 0);

            if (mkdir(dir, mode) < 0 && errno != EEXIST)
                return false;
        }

        delete [] dir;

        return true;
    }
}

void
BoxLib::CreateDirectoryFailed (const std::string& dir)
{
    std::string msg("Couldn't create directory: ");
    msg += dir;
    BoxLib::Error(msg.c_str());
}

void
BoxLib::FileOpenFailed (const std::string& file)
{
    std::string msg("Couldn't open file: ");
    msg += file;
    BoxLib::Error(msg.c_str());
}

void
BoxLib::UnlinkFile (const std::string& file)
{
    unlink(file.c_str());
}

void
BoxLib::OutOfMemory ()
{
#ifdef BL_T3E
    malloc_stats(0);
#endif
#ifdef BL_BGL
    ParallelDescriptor::Abort(12);
#else
    BoxLib::Error("Sorry, out of memory, bye ...");
#endif
}

#if 0
#ifdef WIN32
pid_t
BoxLib::Execute (const char* cmd)
{
  BoxLib::Error("Execute failed!");
  return -1;
}
#else
//extern "C" pid_t fork();

pid_t
BoxLib::Execute (const char* cmd)
{

    pid_t pid = fork();

    if (pid == 0)
    {
        system(cmd);

        exit(0);
    }

    return pid;
}
#endif
#endif


//
// Encapsulates Time
//

namespace
{
const long billion = 1000000000L;
}

BoxLib::Time::Time()
{
    tv_sec = 0;
    tv_nsec = 0;
}

BoxLib::Time::Time(long s, long n)
{
    BL_ASSERT(s >= 0);
    BL_ASSERT(n >= 0);
    BL_ASSERT(n < billion);
    tv_sec = s;
    tv_nsec = n;
    normalize();
}

BoxLib::Time::Time(double d)
{
    tv_sec = long(d);
    tv_nsec = long((d-tv_sec)*billion);
    normalize();
}

double
BoxLib::Time::as_double() const
{
    return tv_sec + tv_nsec/double(billion);
}

long
BoxLib::Time::as_long() const
{
    return tv_sec + tv_nsec/billion;
}

BoxLib::Time&
BoxLib::Time::operator+=(const Time& r)
{
    tv_sec += r.tv_sec;
    tv_nsec += r.tv_nsec;
    normalize();
    return *this;
}

BoxLib::Time
BoxLib::Time::operator+(const Time& r) const
{
    Time result(*this);
    return result+=r;
}

void
BoxLib::Time::normalize()
{
    if ( tv_nsec > billion )
    {
	tv_nsec -= billion;
	tv_sec += 1;
    }
}

BoxLib::Time
BoxLib::Time::get_time()
{
    return Time(BoxLib::wsecond());
}


//
// BoxLib Interface to Mersenne Twistor
//

/* A C-program for MT19937: Real number version (1999/10/28)    */
/*   genrand() generates one pseudorandom real number (double)  */
/* which is uniformly distributed on [0,1]-interval, for each   */
/* call. sgenrand(seed) sets initial values to the working area */
/* of 624 words. Before genrand(), sgenrand(seed) must be       */
/* called once. (seed is any 32-bit integer.)                   */
/* Integer generator is obtained by modifying two lines.        */
/*   Coded by Takuji Nishimura, considering the suggestions by  */
/* Topher Cooper and Marc Rieffel in July-Aug. 1997.            */

/* This library is free software under the Artistic license:       */
/* see the file COPYING distributed together with this code.       */
/* For the verification of the code, its output sequence file      */
/* mt19937-1.out is attached (2001/4/2)                           */

/* Copyright (C) 1997, 1999 Makoto Matsumoto and Takuji Nishimura. */
/* Any feedback is very welcome. For any question, comments,       */
/* see http://www.math.keio.ac.jp/matumoto/emt.html or email       */
/* matumoto@math.keio.ac.jp                                        */

/* REFERENCE                                                       */
/* M. Matsumoto and T. Nishimura,                                  */
/* "Mersenne Twister: A 623-Dimensionally Equidistributed Uniform  */
/* Pseudo-Random Number Generator",                                */
/* ACM Transactions on Modeling and Computer Simulation,           */
/* Vol. 8, No. 1, January 1998, pp 3--30.                          */

namespace
{
// Period parameters
// const int N = 624;
const int M = 397;
const unsigned long MATRIX_A   = 0x9908B0DFUL; // constant vector a
const unsigned long UPPER_MASK = 0x80000000UL; // most significant w-r bits
const unsigned long LOWER_MASK = 0x7FFFFFFFUL; // least significant r bits

// Tempering parameters
const unsigned long TEMPERING_MASK_B = 0x9D2C5680UL;
const unsigned long TEMPERING_MASK_C = 0xEFC60000UL;

inline unsigned long TEMPERING_SHIFT_U(unsigned long y) { return y >> 11L; }
inline unsigned long TEMPERING_SHIFT_S(unsigned long y) { return y << 7L ; }
inline unsigned long TEMPERING_SHIFT_T(unsigned long y) { return y << 15L; }
inline unsigned long TEMPERING_SHIFT_L(unsigned long y) { return y >> 18L; }
}

// initializing the array with a NONZERO seed
void
BoxLib::mt19937::sgenrand(unsigned long seed)
{
#ifdef BL_MERSENNE_ORIGINAL_INIT
    for ( int i = 0; i < N; ++i )
    {
	mt[i] = seed & 0xFFFF0000UL;
	seed  = 69069U * seed + 1;
	mt[i] |= (seed& 0xFFFF0000UL) >> 16;
	seed = 69069U*seed + 1;
    }
#else
    mt[0]= seed & 0xffffffffUL;
    for ( mti=1; mti<N; mti++ ) 
    {
        mt[mti] = (1812433253UL * (mt[mti-1] ^ (mt[mti-1] >> 30L)) + mti); 
        /* See Knuth TAOCP Vol2. 3rd Ed. P.106 for multiplier. */
        /* In the previous versions, MSBs of the seed affect   */
        /* only MSBs of the array mt[].                        */
        /* 2002/01/09 modified by Makoto Matsumoto             */
        mt[mti] &= 0xffffffffUL;       /* for >32 bit machines */
    }
#endif
    mti = N;
}

/* initialize by an array with array-length */
/* init_key is the array for initializing keys */
/* key_length is its length */
void 
BoxLib::mt19937::sgenrand(unsigned long init_key[], int key_length)
{
    int i, j, k;
    sgenrand(19650218UL);
    i=1; j=0;
    k = (N>key_length ? N : key_length);
    for ( ; k; k-- ) 
    {
        mt[i] = (mt[i] ^ ((mt[i-1] ^ (mt[i-1] >> 30)) * 1664525UL)) + init_key[j] + j; /* non linear */
        mt[i] &= 0xffffffffUL; /* for WORDSIZE > 32 machines */
        i++; j++;
        if (i>=N) { mt[0] = mt[N-1]; i=1; }
        if (j>=key_length) j=0;
    }
    for ( k=N-1; k; k-- ) 
    {
        mt[i] = (mt[i] ^ ((mt[i-1] ^ (mt[i-1] >> 30)) * 1566083941UL)) - i; /* non linear */
        mt[i] &= 0xffffffffUL; /* for WORDSIZE > 32 machines */
        i++;
        if (i>=N) { mt[0] = mt[N-1]; i=1; }
    }

    mt[0] = 0x80000000UL; /* MSB is 1; assuring non-zero initial array */ 
}

void
BoxLib::mt19937::reload()
{
    unsigned long y;
    int kk;
    // mag01[x] = x * MATRIX_A  for x=0,1
    static unsigned long mag01[2]={0x0UL, MATRIX_A};
    for ( kk=0; kk<N-M; kk++ )
    {
	y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
	mt[kk] = mt[kk+M] ^ (y >> 1L) ^ mag01[y & 0x1UL];
    }
    for ( ; kk<N-1; kk++ )
    {
	y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
	mt[kk] = mt[kk+(M-N)] ^ (y >> 1L) ^ mag01[y & 0x1UL];
    }
    y = (mt[N-1]&UPPER_MASK)|(mt[0]&LOWER_MASK);
    mt[N-1] = mt[M-1] ^ (y >> 1L) ^ mag01[y & 0x1UL];

    mti = 0;
}

unsigned long
BoxLib::mt19937::igenrand()
{
    // generate N words at one time
    if ( mti >= N ) reload();

    unsigned long y = mt[mti++];
    y ^= TEMPERING_SHIFT_U(y);
    y ^= TEMPERING_SHIFT_S(y) & TEMPERING_MASK_B;
    y ^= TEMPERING_SHIFT_T(y) & TEMPERING_MASK_C;
    y ^= TEMPERING_SHIFT_L(y);

    return y;
}

BoxLib::mt19937::mt19937(unsigned long seed)
    : init_seed(seed), mti(N+1)
{
    sgenrand(seed);
}

BoxLib::mt19937::mt19937 (unsigned long seed_array[], int len)
{
    sgenrand(seed_array, len);
}

void
BoxLib::mt19937::rewind()
{
    sgenrand(init_seed);
}

double
BoxLib::mt19937::d1_value()
{
    return double(igenrand())/0xFFFFFFFFUL;
}

double
BoxLib::mt19937::d_value()
{
    const double zzz = double(0x80000000UL)*2;
    return double(igenrand())/zzz;
}

long
BoxLib::mt19937::l_value()
{
    return igenrand()&0x7FFFFFFFUL;
}

unsigned long
BoxLib::mt19937::u_value()
{
    return igenrand();
}

namespace
{
    BoxLib::mt19937 the_generator;
}

void
BoxLib::InitRandom (unsigned long seed)
{
    the_generator = mt19937(seed);
}

double
BoxLib::Random ()
{
    return the_generator.d1_value();
}

//
// Fortran entry points for BoxLib::Random().
//

BL_FORT_PROC_DECL(BLUTILINITRAND,blutilinitrand)(const int* sd)
{
    unsigned long seed = *sd;
    BoxLib::InitRandom(seed);
}

BL_FORT_PROC_DECL(BLUTILRAND,blutilrand)(Real* rn)
{
    *rn = BoxLib::Random();
}

//
// The standard normal CDF, for one random variable.
//
//   Author:  W. J. Cody
//   URL:   http://www.netlib.org/specfun/erf
//
// This is the erfc() routine only, adapted by the
// transform stdnormal_cdf(u)=(erfc(-u/sqrt(2))/2;
//

static
double
stdnormal_cdf (double u)
{
    static const double Sqrt_2    = 1.41421356237309504880;  // sqrt(2)
    static const double Sqrt_1_Pi = 0.56418958354775628695;  // 1/sqrt(Pi)

    static const double a[5] =
    {
        1.161110663653770e-2,
        3.951404679838207e-1,
        2.846603853776254e+1,
        1.887426188426510e+2,
        3.209377589138469e+3
    };
    static const double b[5] =
    {
        1.767766952966369e-1,
        8.344316438579620e+0,
        1.725514762600375e+2,
        1.813893686502485e+3,
        8.044716608901563e+3
    };
    static const double c[9] =
    {
        2.15311535474403846e-8,
        5.64188496988670089e-1,
        8.88314979438837594e+0,
        6.61191906371416295e+1,
        2.98635138197400131e+2,
        8.81952221241769090e+2,
        1.71204761263407058e+3,
        2.05107837782607147e+3,
        1.23033935479799725e+3
    };
    static const double d[9] =
    {
        1.00000000000000000e+0,
        1.57449261107098347e+1,
        1.17693950891312499e+2,
        5.37181101862009858e+2,
        1.62138957456669019e+3,
        3.29079923573345963e+3,
        4.36261909014324716e+3,
        3.43936767414372164e+3,
        1.23033935480374942e+3
    };
    static const double p[6] =
    {
        1.63153871373020978e-2,
        3.05326634961232344e-1,
        3.60344899949804439e-1,
        1.25781726111229246e-1,
        1.60837851487422766e-2,
        6.58749161529837803e-4
    };
    static const double q[6] =
    {
        1.00000000000000000e+0,
        2.56852019228982242e+0,
        1.87295284992346047e+0,
        5.27905102951428412e-1,
        6.05183413124413191e-2,
        2.33520497626869185e-3
    };

    double y, z;

    y = std::fabs(u);

    if (y <= 0.46875*Sqrt_2)
    {
        //
        // Evaluate erf() for |u| <= sqrt(2)*0.46875
        //
        z = y*y;
        y = u*((((a[0]*z+a[1])*z+a[2])*z+a[3])*z+a[4])
            /((((b[0]*z+b[1])*z+b[2])*z+b[3])*z+b[4]);
        return 0.5+y;
    }

    z = std::exp(-y*y/2)/2;

    if (y <= 4.0)
    {
        //
        // Evaluate erfc() for std::sqrt(2)*0.46875 <= |u| <= std::sqrt(2)*4.0
        //
        y = y/Sqrt_2;
        y = ((((((((c[0]*y+c[1])*y+c[2])*y+c[3])*y+c[4])*y+c[5])*y+c[6])*y+c[7])*y+c[8])
            /((((((((d[0]*y+d[1])*y+d[2])*y+d[3])*y+d[4])*y+d[5])*y+d[6])*y+d[7])*y+d[8]);
        y = z*y;
    }
    else
    {
        //
        // Evaluate erfc() for |u| > std::sqrt(2)*4.0
        //
        z = z*Sqrt_2/y;
        y = 2/(y*y);
        y = y*(((((p[0]*y+p[1])*y+p[2])*y+p[3])*y+p[4])*y+p[5])
            /(((((q[0]*y+q[1])*y+q[2])*y+q[3])*y+q[4])*y+q[5]);
        y = z*(Sqrt_1_Pi-y);
    }

    return u < 0.0 ? y : (1-y);
}

//
// Lower tail quantile for standard normal distribution function.
//
// This function returns an approximation of the inverse cumulative
// standard normal distribution function.  I.e., given P, it returns
// an approximation to the X satisfying P = Pr{Z <= X} where Z is a
// random variable from the standard normal distribution.
//
// The algorithm uses a minimax approximation by rational functions
// and the result has a relative error whose absolute value is less
// than 1.15e-9.
//
// Author:      Peter J. Acklam
// Time-stamp:  2002-06-09 18:45:44 +0200
// E-mail:      jacklam@math.uio.no
// WWW URL:     http://www.math.uio.no/~jacklam
//
// C implementation adapted from Peter's Perl version
//

double
BoxLib::InvNormDist (double p, bool best)
{
    if (p <= 0 || p >= 1)
        BoxLib::Error("BoxLib::InvNormDist(): p MUST be in (0,1)");
    //
    // Coefficients in rational approximations.
    //
    static const double a[6] =
    {
	-3.969683028665376e+01,
        2.209460984245205e+02,
	-2.759285104469687e+02,
        1.383577518672690e+02,
	-3.066479806614716e+01,
        2.506628277459239e+00
    };
    static const double b[5] =
    {
	-5.447609879822406e+01,
        1.615858368580409e+02,
	-1.556989798598866e+02,
        6.680131188771972e+01,
	-1.328068155288572e+01
    };
    static const double c[6] =
    {
	-7.784894002430293e-03,
	-3.223964580411365e-01,
	-2.400758277161838e+00,
	-2.549732539343734e+00,
        4.374664141464968e+00,
        2.938163982698783e+00
    };
    static const double d[4] =
    {
	7.784695709041462e-03,
	3.224671290700398e-01,
	2.445134137142996e+00,
	3.754408661907416e+00
    };

    static const double lo = 0.02425;
    static const double hi = 0.97575;

    double x;

    if (p < lo)
    {
        //
        // Rational approximation for lower region.
        //
        double q = std::sqrt(-2*std::log(p));

        x = (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
            ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1);
    }
    else if (p > hi)
    {
        //
        // Rational approximation for upper region.
        //
        double q = std::sqrt(-2*std::log(1-p));

        x = -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
            ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1);
    }
    else
    {
        //
        // Rational approximation for central region.
        //
        double q = p - 0.5;
        double r = q*q;

        x = (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q /
            (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1);
    }
    //
    // The relative error of the approximation has absolute value less
    // than 1.15e-9.  One iteration of Halley's rational method (third
    // order) gives full machine precision.
    //
    if (best)
    {
        static const double Sqrt_2Pi = 2.5066282746310002;  // sqrt(2*Pi)

        double e = stdnormal_cdf(x) - p;
        double u = e*Sqrt_2Pi*std::exp(x*x/2);

        x -= u/(1 + x*u/2);
    }

    return x;
}

//
// Fortran entry points for BoxLib::InvNormDist().
//

BL_FORT_PROC_DECL(BLINVNORMDIST,blinvnormdist)(Real* result)
{
    double val;
    //
    // Get a random number in (0,1);
    //
    do
    {
        val = the_generator.d_value();
    }
    while (val == 0);

    *result = BoxLib::InvNormDist(val,false);
}

BL_FORT_PROC_DECL(BLINVNORMDISTBEST,blinvnormdistbest)(Real* result)
{
    double val;
    //
    // Get a random number in (0,1);
    //
    do
    {
        val = the_generator.d_value();
    }
    while (val == 0);

    *result = BoxLib::InvNormDist(val,true);
}


//
// Sugar for parsing IO
//

std::istream&
BoxLib::operator>>(std::istream& is, const expect& exp)
{
    int len = exp.istr.size();
    int n = 0;
    while ( n < len )
    {
	char c;
	is >> c;
	if ( !is ) break;
	if ( c != exp.istr[n++] )
	{
	    is.putback(c);
	    break;
	}
    }
    if ( n != len )
    {
	is.clear(std::ios::badbit|is.rdstate());
	std::string msg = "expect fails to find \"" + exp.the_string() + "\"";
	BoxLib::Error(msg.c_str());
    }
    return is;
}

BoxLib::expect::expect(const char* istr_)
    : istr(istr_)
{
}

BoxLib::expect::expect(const std::string& str_)
    : istr(str_)
{
}

BoxLib::expect::expect(char c)
{
    istr += c;
}

const std::string&
BoxLib::expect::the_string() const
{
    return istr;
}
