
#ifndef _MINISCHEDULER_H_
#define _MINISCHEDULER_H_

#include "Mutex.h"
#include "eastl/vector.h"

//for timer:
#include "pixeltoaster/PixelToaster.h"

#define MAX_THREADS					 32	/* maximum number of worker threads we expect to encounter */
#define MAX_TASKSPERTHREAD			256	/* maximum capacity of a worker thread's task queue. Past this amount, tasks are executed immediately instead of being queued */

#define VECTOR_SIZE				16
#define CACHELINE_SIZE			64
#define VECTOR_ALIGNED			__declspec(align(VECTOR_SIZE))
#define CACHELINE_ALIGNED		__declspec(align(CACHELINE_SIZE))

/* Thread local temp space */
#define THREAD_TEMP_SPACE_SIZE	(16*1024*1024)	/* temp space to build textures, meshes, etc		*/
#define THREAD_HEAP_SPACE_SIZE	(16*1024*1024)	/* temp space to do allocation from worker threads	*/

#define WORKERTHREAD_PROFILER 1

class TaskPool;
class WorkerThread;
class InternalTask;

//________________________________________________________________________________
class CACHELINE_ALIGNED TaskCompletion {
public:
   TaskCompletion() {
      mBusy = 0;
   }
   bool isBusy() const {
      return mBusy != 0;
   }
   void markBusy( bool bBusy ) {
      if( bBusy )
         InterlockedIncrement( &mBusy );
      else
         InterlockedDecrement( &mBusy );
   }
private:
   volatile LONG mBusy;
   /* uses whole cache line to avoid false sharing */
};

//________________________________________________________________________________
class WorkerThread {
   friend class TaskPool;
public:
   bool start( TaskPool* pTaskPool );
   static WorkerThread *getCurrent();
   int getWorkerIndex();

public:
   static DWORD WINAPI threadProcWinApiWrapper(void* p);
   DWORD threadProc();
   void idle();
   
   bool init( TaskPool* pTaskPool );
   bool attachToThisThread( TaskPool* pTaskPool );
   
   bool pushTask( InternalTask* pTask );             /* queue task if there is space, and run it otherwise */
   bool pushTaskOrDoNothing( InternalTask* pTask );  /* queue task if there is space (or do nothing) */
   
   bool popTask( InternalTask** ppTask );				   /* pop task from queue */
   bool stealTasks();                                 /* fill queue with work from another thread */
   bool giveUpSomeWork( WorkerThread *pIdleThread );	/* request from an idle thread to give up some work */
   
   void workUntilDone( TaskCompletion* pCard );
   void doWork( TaskCompletion* pCard );
   
public:
   /* task list */
   CSpinMutex mTaskMutex;
   InternalTask* mTasks[MAX_TASKSPERTHREAD];	/* tasks queue for this thread (pop from top of pile, steal from bottom)*/
   unsigned mTaskCount;				       /* number of tasks currently in queue */
   TaskCompletion* mpCurrentCompletion; /* completion flag for currently running task */

   /* heap */
   SLIST_HEADER mFreeList;
   HANDLE mhHeap;

   /* misc */
   TaskPool* mpTaskPool;
   HANDLE mhThread;
   volatile bool mbFinished;

#ifdef WORKERTHREAD_PROFILER
   enum ProfileEventType {
      eThreadStarted,
      eThreadStopped,
      eMutexWaitStart,
      eMutexWaitEnd,
      eMutexReleased,
      eMutexDestroyed,
      eSemaphoreWaitStart,
      eSemaphoreWaitEnd,
      eSemaphoreSignalled,
      eSemaphoreDestroyed,
      eEventWaitStart,
      eEventWaitEnd,
      eSleep,
      eFrameStart,
      eFrameEnd,
      eWorking,
      eIdle
   };
   typedef struct {
      ProfileEventType type;
      double time;
   } ProfileEvent;
   static double mFrameEndTime;
   eastl::vector<ProfileEvent> mProfileEvents;
   static PixelToaster::Timer mProfileTimer;

   static void profilerStartFrame() {
      mProfileTimer.reset();
   }
   static void profilerEndFrame() {
      mFrameEndTime = mProfileTimer.time();
   }

   static void drawProfiler(TaskPool *taskPool, uint32_t *pixelbuffer, uint_t width, uint_t height);

   void insertProfilerEvent(ProfileEventType type) {
      ProfileEvent evt;
      evt.type = type;
      evt.time = mProfileTimer.time();
      mProfileEvents.pushBack(evt);
   }
#endif

};

//________________________________________________________________________________
class OverloadNewDeletePerThread {
public:
   virtual ~OverloadNewDeletePerThread() {}

   void* operator new( size_t size ) {
      return OperatorNew( size );
   }
   void operator delete( void* p ) {
      OperatorDelete( p );
   }
   
   /* we never call new OverloadNewDeletePerThread[...]						    */
   /* TODO: Should the need arise, this needs to be implemented differently */
   /*       (wasted space, every struct holds heap handle)						 */
   void* operator new[]( size_t size )
   {
      EASTL_ASSERT( false );
      return OperatorNew( size );
   }
   void operator delete[]( void* p )
   {
      EASTL_ASSERT(false);
      OperatorDelete( p );
   }

   static void* OperatorNew( size_t size );
   static void OperatorDelete( void* p );

protected:
   union {
      void* mpHeapData;
      SLIST_ENTRY mFreeListEntry;
   };
};

//________________________________________________________________________________
class InternalTask : public OverloadNewDeletePerThread {
public:
   InternalTask( TaskCompletion* pCompletion ) {
      mCompletion = pCompletion;
   }

   /* does its work and suicides (or recycles)	*/
   virtual bool run( WorkerThread* pThread ) =0;
   
   /* Keep half the work and put the other half in a new task	*/
   virtual bool split( WorkerThread* pThread, InternalTask** ppTask ) {
      return false;
   }

   /* returns a sub part of the task */
   virtual bool partialPop( WorkerThread* pThread, InternalTask** ppTask ) {
      return false;
   }

   /* share work across all threads (pool is idle) */
   virtual bool spread( TaskPool* pPool ) {
      return false;
   }

public:
   TaskCompletion* mCompletion;
};

class TaskPool {
public:
   TaskPool();
   
   bool start( int MaxThreadCount=0 );
   bool setThreadCount( int ThreadCount );
   int setThreadCount();
   bool stop();
   
   void waitForWorkersToBeReady();
   void wakeWorkers();
   
   static unsigned getHardwareThreadsCount();

   unsigned getNumberThreads() {
      return mThreadCount;
   }
   WorkerThread *getThread(int number) {
      return &mThread[number];
   }

public:
   WorkerThread mThread[MAX_THREADS];
   HANDLE mSleepNotification;
   HANDLE mWakeUpCall;
   uint_t mThreadCount;
   bool mbShuttingDown;
   
   bool mbWorkersIdle;
   volatile TaskCompletion* mMainCompletion;

   static DWORD msTLSindex;
   static bool msbNeverIdle;
};

inline void TaskPool::wakeWorkers() {
   if( msbNeverIdle )
      return;

   EASTL_ASSERT( mbWorkersIdle );

   mbWorkersIdle = false;
   ReleaseSemaphore( mWakeUpCall, mThreadCount - 1, NULL );
}

inline void TaskPool::waitForWorkersToBeReady() {
   if( msbNeverIdle || mbWorkersIdle == true)
      return;

   for( unsigned i = 0; i < mThreadCount - 1; i++ )
      WaitForSingleObject( mSleepNotification, INFINITE );

   mbWorkersIdle = true;
}

//*****************************************************

inline int WorkerThread::getWorkerIndex() {
   return int( this - mpTaskPool->mThread );
}

#endif // _MINISCHEDULER_H_
