
#include "Mutex.h"
#include "MiniScheduler.h"
#include "ParallelFor.h"

class InternalForTask : public InternalTask {
public:
   InternalForTask( TaskCompletion* pCompletion,
                   const eastl::FastDelegate2<WorkerThread*, const ParallelRange&, bool> callback,
                   //const ParallelForTask* pForTask,
                   const ParallelRange& Range ) : InternalTask( pCompletion ),
      mCallback( callback ),
      mRange( Range ) {}

   bool run( WorkerThread* pThread ) {
      bool bOk;
      
      //bOk = mForTask->doRange( pThread, mRange );
      bOk = mCallback(pThread, mRange);
      
      delete this;
      return bOk;
   }
   
   bool split( WorkerThread* pThread, InternalTask** ppTask ) {
      InternalForTask* pNew;
      int CutIndex;
      
      *ppTask = NULL;

      /* check we can split */
      if( mRange.mEnd - mRange.mBegin < 2 * mRange.mGranularity )
         return false;
      
      /* decide on cut point */
      CutIndex = ( mRange.mBegin + mRange.mEnd ) / 2;
      
      /* create new task and reduce our range*/
      pNew = new InternalForTask( mCompletion, mCallback, ParallelRange( CutIndex, mRange.mEnd, mRange.mGranularity ) );
      mRange.mEnd = CutIndex;

      /* done */
      *ppTask = pNew;
      return true;
   }
   
   bool partialPop( WorkerThread* pThread, InternalTask** ppTask ) {
      InternalForTask* pNew;
      int CutIndex;
      
      *ppTask = NULL;

      /* check we can split */
      if( mRange.mEnd - mRange.mBegin < 2 * mRange.mGranularity )
         return false;
      
      /* decide on cut point */
      CutIndex = mRange.mBegin + mRange.mGranularity;
      
      /* create new task and reduce our range*/
      pNew = new InternalForTask( mCompletion, mCallback, ParallelRange( mRange.mBegin, CutIndex,
                                                                           mRange.mGranularity ) );
      mRange.mBegin = CutIndex;

      /* done */
      *ppTask = pNew;
      return true;
   }
   
   bool spread( TaskPool* pPool ) {
      WorkerThread* pThread;
      int begin, end, granularity;
      unsigned PartSize, Rest;
      unsigned iThread, Count;
      
      /* count parts */
      begin = mRange.mBegin;
      end = mRange.mEnd;
      granularity = mRange.mGranularity;
      
      Count = ( mRange.mEnd - mRange.mBegin ) / mRange.mGranularity;
      if( Count > pPool->mThreadCount )
         Count = pPool->mThreadCount;

      PartSize = ( end - begin ) / Count;
      Rest = ( end - begin ) - Count * PartSize;

      /* add a task per thread */
      for( iThread = 0; iThread < Count; iThread++ ) {
         InternalForTask* pTask;
         unsigned Size;
         
         /* compute this chunk's size */
         Size = PartSize + int( Rest > iThread );
         
         /* on thread zero use this object, create a task each other thread */
         if( iThread ) {
            pTask = new InternalForTask( mCompletion, mCallback, ParallelRange( begin, begin + Size, granularity ) );
         } else {
            pTask = this;
            mRange = ParallelRange( begin, begin + Size, granularity );
         }

         /* queue work */
         pThread = &pPool->mThread[iThread];
         {
            CSpinMutexLock L( &pThread->mTaskMutex );

            mCompletion->markBusy( true );
            pThread->mTasks[0] = pTask;
            pThread->mTaskCount = 1;
         }

         /* prep next loop */
         begin += Size;
      }
      return true;
   }

public:
   //const ParallelForTask* mForTask;
   const eastl::FastDelegate2<WorkerThread*, const ParallelRange&, bool> mCallback;
   ParallelRange mRange;
};

bool ParallelFor( //const ParallelForTask* pTask,
                  const eastl::FastDelegate2<WorkerThread*, const ParallelRange&, bool> callback,
                  const ParallelRange& Range ) {
   return ParallelFor( WorkerThread::getCurrent(), callback, Range );
}

bool ParallelFor( WorkerThread* pThread,
                  //const ParallelForTask* pTask,
                  const eastl::FastDelegate2<WorkerThread*, const ParallelRange&, bool> callback,
                  const ParallelRange& Range ) {
   TaskCompletion Completion;
   InternalForTask* pInternalTask;
   
   pInternalTask = new InternalForTask( &Completion, callback, Range );
   pThread->pushTask( pInternalTask );
   pThread->workUntilDone( &Completion );

   return true;
}

