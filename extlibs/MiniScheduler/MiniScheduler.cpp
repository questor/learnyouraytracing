
#include "Mutex.h"
#include "MiniScheduler.h"

#include <float.h>

/* worker threads stack size */
#define STACKSIZE 64*1024 /* 64K */

DWORD   TaskPool::msTLSindex;
bool    TaskPool::msbNeverIdle;


double WorkerThread::mFrameEndTime;
PixelToaster::Timer WorkerThread::mProfileTimer;
void WorkerThread::drawProfiler(TaskPool *taskPool, uint32_t *pixelbuffer, uint_t width, uint_t height) {
   uint_t xGap = 40;
   uint_t yGap = 10;
   uint_t ySize = 10;
   uint_t numberThreads = taskPool->getNumberThreads();
   uint_t yPos = height-(numberThreads+1)*(ySize+yGap);

   taskPool->waitForWorkersToBeReady();

   for(uint_t i=0; i<numberThreads; ++i) {
      WorkerThread *thread = taskPool->getThread(i);
      //printf("thread %i   frameEndTime %g    size %i\n", i, mFrameEndTime, thread->mProfileEvents.size());

      uint_t j=0;
      uint_t xPos = xGap;
      uint_t fillColor = 0xff555555;
      if(thread->mProfileEvents.size() != 0) {
         while(j < thread->mProfileEvents.size()-1) {
            //printf("event: %g %d\n", thread->mProfileEvents[j].time, thread->mProfileEvents[j].type);
            uint_t nextEvtPos = xGap + (uint_t)(thread->mProfileEvents[j].time/mFrameEndTime * (width-2*xGap));
            while(xPos < nextEvtPos) {
               pixelbuffer[yPos*width+xPos] = fillColor;
               xPos++;
            }
            switch(thread->mProfileEvents[j].type) {
            case WorkerThread::eWorking: fillColor = 0xffffffff; break;
            case WorkerThread::eIdle:    fillColor = 0xff555555; break;
            default:
               EASTL_ASSERT(0);
            }

            j++;
         }
         while(xPos < width-2*xGap) {
            pixelbuffer[yPos*width+xPos] = fillColor;
            xPos++;
         }
         for(j=1; j<ySize; j++) {
            memcpy(pixelbuffer+(yPos+j)*width+xGap, pixelbuffer+yPos*width+xGap, (width-2*xGap)*sizeof(uint32_t));
         }
      }
      thread->mProfileEvents.clear();

      yPos += ySize+yGap;
   }
}

//________________________________________________________________________________
TaskPool::TaskPool()
{
   mThreadCount = 0;
   mbShuttingDown = false;
}

unsigned TaskPool::getHardwareThreadsCount()
{
   SYSTEM_INFO si;

   GetSystemInfo( &si );
   return si.dwNumberOfProcessors;
}

bool TaskPool::setThreadCount( int ThreadCount )
{
   if( !ThreadCount )
      ThreadCount = getHardwareThreadsCount();
   
   if( ThreadCount == mThreadCount )
      return true;

   stop();
   start( ThreadCount );

   return true;
}

int TaskPool::setThreadCount()
{
   return mThreadCount;
}

bool TaskPool::start( int MaxThreadCount )
{
   unsigned iThread;
   
   mbShuttingDown = false;
   mbWorkersIdle = false;
   mMainCompletion = NULL;
   
   msbNeverIdle = false;
   
   /* find hardware thread count */
   mThreadCount = MaxThreadCount ? MaxThreadCount : getHardwareThreadsCount();
   if( mThreadCount > MAX_THREADS )
      mThreadCount = MAX_THREADS;
   
   /* initialize Thread Local Storage */
   /* (TLS lets us store/query the CWorkerThread* corresponding to current thread) */
   msTLSindex = TlsAlloc();
   if( TLS_OUT_OF_INDEXES == msTLSindex ) return false;

   /* Worker wakeup event */
   mWakeUpCall = CreateSemaphore( NULL, 0, MAX_THREADS, NULL );
   mSleepNotification = CreateSemaphore( NULL, 0, MAX_THREADS, NULL );

   /* set ourselves up as thread[0] */
   mThread[0].attachToThisThread( this );
   
   /* start worker threads */
   for( iThread = 1; iThread < mThreadCount; iThread++ )
   {
      mThread[iThread].start( this );
   }
   
   return true;
}

bool TaskPool::stop()
{
   unsigned iThread;

   mbShuttingDown = true;

   /* wait for all to finish */
   waitForWorkersToBeReady();
   wakeWorkers();
   for( iThread = 1; iThread < mThreadCount; iThread++ )
   {
      while( !mThread[iThread].mbFinished )
      {/* spin */;
      }
   }

   TlsFree( msTLSindex );
   msTLSindex = TLS_OUT_OF_INDEXES;
   return true;
}

//________________________________________________________________________________
bool WorkerThread::init( TaskPool* pTaskPool ) {
   mpTaskPool = pTaskPool;
   mTaskCount = 0;
   mbFinished = false;
   mpCurrentCompletion = NULL;

   /* local heap */
   mhHeap = HeapCreate( 0, 0, THREAD_HEAP_SPACE_SIZE );
   if( !mhHeap ) return false;
   
   InitializeSListHead( &mFreeList );
   
   return true;
}

bool WorkerThread::attachToThisThread( TaskPool* pTaskPool ) {
   bool bOk;
   
   bOk = init( pTaskPool );
   if(bOk == false)
      return bOk;

   TlsSetValue( mpTaskPool->msTLSindex, this );

   mhThread = GetCurrentThread();
   ASSERT( mhThread );
   
   return mhThread != NULL;
}

bool WorkerThread::start( TaskPool* pTaskPool ) {
   bool bOk;
   DWORD ThreadId;
   
   bOk = init( pTaskPool );
   if(bOk == false)
      return bOk;
   
   mhThread = CreateThread( NULL, STACKSIZE, threadProcWinApiWrapper, this, 0, &ThreadId );
   ASSERT( mhThread );
   
   return mhThread != NULL;
}

WorkerThread* WorkerThread::getCurrent() {
   return ( WorkerThread* )TlsGetValue( TaskPool::msTLSindex );
}

DWORD WINAPI WorkerThread::threadProcWinApiWrapper( void* p ) {
   WorkerThread* pThread = ( WorkerThread* )p;
   return pThread->threadProc();
}

void WorkerThread::idle() {
   if( TaskPool::msbNeverIdle )
      return;
   
   /* Advertise we're going to sleep */
   ReleaseSemaphore( mpTaskPool->mSleepNotification, 1, NULL );
   
   /* Sleep */
   WaitForSingleObject( mpTaskPool->mWakeUpCall, INFINITE );
}

DWORD WorkerThread::threadProc() {
   /* Because of DX9 we have to set the fpu to single-precision and round-to-nearest as
      the mainthread is changed to this mode by dx9 */
   _controlfp(_PC_24, MCW_PC);   //single precision
   _controlfp(_RC_NEAR, MCW_RC); //round-to-nearest

   /* Thread Local Storage */
   TlsSetValue( mpTaskPool->msTLSindex, this );

   /* run */
   for(; ; ) {
      idle();

      /* check if we're shutting down */
      if( mpTaskPool->mbShuttingDown )
         break;

      while( mpTaskPool->mMainCompletion ) {
         /* do work */
         doWork( NULL );

         /* check if we're shutting down */
         if( mpTaskPool->mbShuttingDown )
            break;

#if 0
         /* there isn't work for everybody			*/
         /* less performance but saves some power	*/
         if (m_pTaskPool->m_pMainCompletion)
            Sleep(1);
#endif
      }
   }
   
   mbFinished = true;
   return 0;
}

void WorkerThread::doWork( TaskCompletion* pExpected ) {
/* NOTE:
    If pExpected is NULL, then we'll work until there is nothing left to do. This 
    is normally happening only in the case of a worker's thread loop (above).
    
    if it isn't NULL, then it means the caller is waiting for this particular thing 
    to complete (and will want to carry on something once it is). We will do our work
    and steal some until the condition happens. This is normally happening when as
    part of WorkUntilDone (below)
   
    NOTE: This method needs to be reentrant (!)
    A task can be spawing more tasks and may have to wait for their completion.
    So, as part of our pTask->Run() we can be called again, via the WorkUntilDone 
    method, below. */
   
   insertProfilerEvent(eWorking);
   do {
      InternalTask* pTask;
      TaskCompletion* pLastCompletion;
      
      pLastCompletion = NULL;
      while( popTask( &pTask ) ) {
         /* do something */
         /* TASK */ pLastCompletion = mpCurrentCompletion;
         /* TASK */ mpCurrentCompletion = pTask->mCompletion;
         /* TASK */
         /* TASK */ pTask->run( this );
         /* TASK */
         /* TASK */ mpCurrentCompletion->markBusy( false );
         /* TASK */ mpCurrentCompletion = pLastCompletion;
         
         /* check if work we're expecting is done */
         if( pExpected && !pExpected->isBusy() )
            return;
      }

      /* check if main work is finished */
      if( !mpTaskPool->mMainCompletion )
         return;
   } while( stealTasks() );
   insertProfilerEvent(eIdle);
   /* Nothing left to do, for now */
}

void WorkerThread::workUntilDone( TaskCompletion* pCard )
{
   while( pCard->isBusy() ) {
      doWork( pCard );
   }
   
   if( mpTaskPool->mMainCompletion == pCard )
   {	/* This is the root task. As this is finished, the scheduler can go idle.	  */
      /* What happens next: (eventually,) each worker thread will see that there	  */
      /* is no main completion any more and go idle waiting for semaphore to signal */
      /* that new work nees to be done (see CWorkerThread::ThreadProc)				*/
      mpTaskPool->mMainCompletion = NULL;
   }
}

bool WorkerThread::popTask( InternalTask** ppOutTask )
{
   CSpinMutexLock L( &mTaskMutex );
   InternalTask* pTask;
   
   /* Check there is work */
   if( !mTaskCount )
      return false;

   pTask = mTasks[mTaskCount - 1];
   
   /* Check if we can pop a partial-task (ex: one iteration of a loop) */
   if( pTask->partialPop( this, ppOutTask ) )
   {
      pTask->mCompletion->markBusy( true );
      return true;
   }
   
   /* pop top of pile */
   *ppOutTask = pTask;
   mTaskCount--;
   return true;
}

bool WorkerThread::pushTaskOrDoNothing( InternalTask* pTask )
{
   /* if we're single threaded, ignore */
   if( mpTaskPool->mThreadCount < 2 )
      return false;

   /* if task pool is empty, try to spread subtasks across all threads */
   if( !mpTaskPool->mMainCompletion ) {
      /* check we're indeed the main thread			*/
      /* (no worker can push job, no task is queued)	*/
      ASSERT( getWorkerIndex() == 0 );
      
      /* Ready ? */
      mpTaskPool->waitForWorkersToBeReady();
      
      /* Set... */
      if( pTask->spread( mpTaskPool ) )
      {	/* Go! Mark this task as the root task (see WorkUntilDone) */
         mpTaskPool->mMainCompletion = pTask->mCompletion;
         mpTaskPool->wakeWorkers();
         return true;
      }
   }
   
   /* push work onto pile */
   {
      CSpinMutexLock L( &mTaskMutex );
      
      /* don't queue more than we can */
      if( mTaskCount >= MAX_TASKSPERTHREAD ) {
         EASTL_ASSERT(!"can't run more jobs because tasksPerThread is full!");
         return false;
      }
      
      /* push job */
      pTask->mCompletion->markBusy( true );
      mTasks[mTaskCount] = pTask;
      mTaskCount++;
   }
   
   /* Go ! */
   if( !mpTaskPool->mMainCompletion )
   {	/* Mark this task as the root task (see WorkUntilDone) */
      mpTaskPool->mMainCompletion = pTask->mCompletion;
      mpTaskPool->wakeWorkers();
   }
   
   return true;
}

bool WorkerThread::pushTask( InternalTask* pTask )
{
   if( pushTaskOrDoNothing( pTask ) )
      return true;

   /* if we can't queue it, run it */
   /* (NOTE: we don't touch completion card: not set, not cleared) */
   insertProfilerEvent(eWorking);      //for single threaded work
   pTask->run( this );
   insertProfilerEvent(eIdle);
   return false;
}

bool WorkerThread::giveUpSomeWork( WorkerThread* pIdleThread )
{	
   CSpinMutexLock L( &mTaskMutex );

   /* anything to share ? */
   if( !mTaskCount ) return false;

   /* grab work */
   unsigned GrabCount;
   unsigned iTask;
   InternalTask** p;
   CSpinMutexLock LockIdleThread( &pIdleThread->mTaskMutex );
   
   if( pIdleThread->mTaskCount )
      return false; /* can happen if we're trying to steal work while taskpool has gone idle and started again */
   
   /* if only one task remaining, try to split it */
   if( mTaskCount == 1 )
   {
      InternalTask* pTask;
      
      pTask = NULL;
      if( mTasks[0]->split( pIdleThread, &pTask ) ) {
         pTask->mCompletion->markBusy( true );

         pIdleThread->mTasks[0] = pTask;
         pIdleThread->mTaskCount = 1;
         return true;
      }
   }

   /* grab half the remaining tasks (rounding up) */
   GrabCount = ( mTaskCount + 1 ) / 2;

   /* copy old tasks to my list */
   p = pIdleThread->mTasks;
   for( iTask = 0; iTask < GrabCount; iTask++ ) {
      *p++ = mTasks[iTask];
      mTasks[iTask] = NULL;
   }
   pIdleThread->mTaskCount = GrabCount;
   
   /* move remaining tasks down */
   p = mTasks;
   for(; iTask < mTaskCount; iTask++ ) {
      *p++ = mTasks[iTask];
   }
   mTaskCount -= GrabCount;
   
   return true;
}

bool WorkerThread::stealTasks() {
   unsigned iThread;
   int Offset;

   /* avoid always starting with same other thread. This aims at avoiding a potential	*/
   /* for problematic patterns. Note: the necessity of doing is largely speculative.	*/
   Offset = ( getWorkerIndex() + GetTickCount() ) % mpTaskPool->mThreadCount;
   
   /*  */
   for( iThread = 0; iThread < mpTaskPool->mThreadCount; iThread++ ) {
      WorkerThread* pThread;
      
      pThread = &mpTaskPool->mThread[ ( iThread + Offset ) % mpTaskPool->mThreadCount ];
      if( pThread == this )
         continue;

      if( pThread->giveUpSomeWork( this ) )
         return true;

      if( mTaskCount )
         return true;
   }
   
   return false;
}

//________________________________________________________________________________
void* OverloadNewDeletePerThread::OperatorNew( size_t size ) {
   WorkerThread* pThread;
   OverloadNewDeletePerThread* pThis;
   PSLIST_ENTRY pFree;

   pThread = WorkerThread::getCurrent();
   
   /* free anything that needs to be freed */
   for(; ; ) {
      pFree = InterlockedPopEntrySList( &pThread->mFreeList );
      if( !pFree ) break;
      
      HeapFree( pThread->mhHeap, HEAP_NO_SERIALIZE, pFree );
   }

   /* allocate */
   pThis = ( OverloadNewDeletePerThread* )HeapAlloc( pThread->mhHeap, HEAP_NO_SERIALIZE, size );
   pThis->mpHeapData = pThread;
   
   return pThis;
}

void OverloadNewDeletePerThread::OperatorDelete( void* p ) {
   OverloadNewDeletePerThread* pThis = ( OverloadNewDeletePerThread* )p;
   WorkerThread* pThread = ( WorkerThread* )pThis->mpHeapData;
   
   InterlockedPushEntrySList( &pThread->mFreeList, ( SLIST_ENTRY* )p );
}
