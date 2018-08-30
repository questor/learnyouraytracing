
#ifndef _MUTEX_H_
#define _MUTEX_H_

#include "eastl/extra/debug.h"

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#include <windows.h>

#define MUTEX_CRITICALSECTIONWITHSPINCOUNT	1
#define MUTEX_CRITICALSECTION                2
#define MUTEX_SIMPLESPIN                     3
#define MUTEX_FIRSTCOMEFIRSTSERVE         	4

#define MUTEX_METHOD	MUTEX_CRITICALSECTIONWITHSPINCOUNT
// #define MUTEX_METHOD	MUTEX_CRITICALSECTION
// #define MUTEX_METHOD	MUTEX_SIMPLESPIN
// #define MUTEX_METHOD	MUTEX_FIRSTCOMEFIRSTSERVE

//________________________________________________________________________________
#if MUTEX_METHOD == MUTEX_CRITICALSECTIONWITHSPINCOUNT
class CSpinMutex {
public:
   CSpinMutex() {
      InitializeCriticalSectionAndSpinCount( &mCS, 128 );
   }
   void lock() {
      EnterCriticalSection( &mCS );
   }
   void unlock() {
      LeaveCriticalSection( &mCS );
   }
public:
   CRITICAL_SECTION mCS;
};

#elif MUTEX_METHOD == MUTEX_CRITICALSECTION
class CSpinMutex {
public:
   CSpinMutex() {
      InitializeCriticalSection(&cs);
   }
   void lock()	{
      EnterCriticalSection(&cs);
   }
   void unlock() {
      LeaveCriticalSection(&cs);
   }
public:
   CRITICAL_SECTION cs;
};

#elif MUTEX_METHOD == MUTEX_SIMPLESPIN
class CACHELINE_ALIGNED CSpinMutex {
public:
   CSpinMutex() {
      m_Locked = 0;
   }
   void lock()	{
      while (InterlockedExchange(&m_Locked, 1) != 0) { _mm_pause(); }
   }
   void unlock() {
      InterlockedExchange(&m_Locked, 0);
   }

public:
   volatile LONG m_Locked;
   /* uses all of cache line to avoid false sharing */
};

#elif MUTEX_METHOD == MUTEX_FIRSTCOMEFIRSTSERVE
class CACHELINE_ALIGNED CSpinMutex {
public:
   CSpinMutex() {
      m_LastTicket	= 0;
      m_Calling		= 1;
   }
   void lock() {
      LONG Ticket = InterlockedIncrement(&m_LastTicket);
      for(;;)
      {
         if (Ticket==m_Calling) return;
         _mm_pause();
      }
   }
   void unlock() {
      m_Calling++;
   }

protected:
   volatile	LONG	m_LastTicket;
   volatile	LONG	m_Calling;
};

#endif

class CSpinMutexLock {
public:
   CSpinMutexLock( CSpinMutex* pMutex ) {
      m_pMutex = pMutex;
      m_pMutex->lock();
   }
   ~CSpinMutexLock() {
      m_pMutex->unlock();
   }

private:
   /* you don't want to copy a lock, do you? */
   CSpinMutexLock( const CSpinMutexLock& L ) {
      EASTL_ASSERT( false );
   }
   CSpinMutexLock& operator =( const CSpinMutexLock& L ) {
      EASTL_ASSERT( false ); return *this;
   }
   
public:
   CSpinMutex* m_pMutex;
};

#endif // _MUTEX_H_
