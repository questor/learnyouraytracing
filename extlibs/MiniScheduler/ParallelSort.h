
#ifndef _MINIPARALLEL_SORT_H_
#define _MINIPARALLEL_SORT_H_

#include "TaskScheduler.h"

template <class T, class Comparator, int MinItemCountPerTask> class CSorterInternalTask;

template <class T, class Comparator, int MinItemCountPerTask> class ParallelSorter {
public:
   ParallelSorter( T* pData,
           unsigned Count )
   {
      mData = pData;
      mCount = Count;
      mToFree = malloc( sizeof( T ) * Count );
      mTemp = ( T* )mToFree;
   }
   
   ParallelSorter( T* pData,
           unsigned Count,
           T* pTemp )
   {
      mData = pData;
      mCount = Count;
      mToFree = NULL;
      mTemp = pTemp;
   }
   
   ~ParallelSorter()
   {
      if( mToFree )
         free( mToFree );
   }
   
   void sort();
   void sortSerial(); /* ie not using task scheduler */
   
public:
   T* mData;
   T* mTemp;
   void* mToFree;
   unsigned mCount;
};

template <class T, class Comparator, int MinItemCountPerTask> class CSorterInternalTask : public CInternalTask {
public:
   enum eTaskType
   {
      TaskType_Sort,			/* sort the buffer passed	*/
      TaskType_MergeFront,	/* merge the "front" half	*/
      TaskType_MergeBack		/* merge the "back" half	*/
   };

   CSorterInternalTask( CTaskCompletion* pCompletion ) : CInternalTask( pCompletion ) {}

   void init( CSorter <T, Comparator, MinItemCountPerTask>* pSorter,
             int Start, int Count, eTaskType TaskType = TaskType_Sort ) {
      mpSorter = pSorter;
      mTaskType = TaskType;
      mStart = Start;
      mCount = Count;
   }

   void merge( int iA, int nA, int iB, int nB ) {
      T* p;
      T* pData = mpSorter->mData;
      T* pTemp = mpSorter->mTemp;
      int iC, nC;
      
      /* Find first item not already at its place */
      while( nA )
      {
         if( Comparator::isCorrectOrder( pData[iB], pData[iA] ) )
            break;
         
         iA++;
         nA--;
      }
      
      if( !nA ) return/* already sorted */;
      
      /* merge what needs to be merged in pTemp */
      iC = iA;
      p = &pTemp[iA];
      
      *p++ = pData[iB++]; /* first one is from B */
      nB--;

      while( nA && nB )
      {
         if( Comparator::isCorrectOrder( pData[iB], pData[iA] ) )
         {
            *p++ = pData[iB++];
            nB--;
         }
         else
         {
            *p++ = pData[iA++];
            nA--;
         }
      }
      
      /* if anything remains in B, it already is in correct order inside pData */
      /* if anything is left in A, it needs to be moved */
      memcpy( p, &pData[iA], nA * sizeof( T ) );
      p += nA;
      
      /* copy what has changed position back from pTemp to pData */
      nC = int( p - &pTemp[iC] );
      memcpy( &pData[iC], &pTemp[iC], nC * sizeof( T ) );
   }

   void swap( T& A, T& B ) {
      T C;
      C = A;
      A = B;
      B = C;
   }
   
   void sort( int start, int count ) {
      int half;

      switch( count )
      {
      /* case 0:				*/
      /* case 1: 				*/
      /* {					*/
      /* 	ASSERT("WTF?");		*/
      /* 	return;				*/
      /* }					*/

      case 2:
      {
         T* pData = mpSorter->mData + start;

         if( Comparator::isCorrectOrder( pData[1], pData[0] ) )
            swap( pData[0], pData[1] );

      }	break;
         
      case 3:
      {
         T* pData = mpSorter->mData + start;
         T tmp;

         int k = ( Comparator::isCorrectOrder( pData[1], pData[0] ) ?0 :1 )
               + ( Comparator::isCorrectOrder( pData[2], pData[0] ) ?0 :2 )
               + ( Comparator::isCorrectOrder( pData[2], pData[1] ) ?0 :4 );

         switch( k )
         {
         case 7: /* ABC */
            break;
         case 3: /* ACB */
            swap( pData[1], pData[2] );
            break;
         case 6: /* BAC */
            swap( pData[0], pData[1] );
            break;
         case 1: /* BCA */
            tmp = pData[0]; pData[0] = pData[2]; pData[2] = pData[1]; pData[1] = tmp;
            break;
         case 4: /* CAB */
            tmp = pData[0]; pData[0] = pData[1]; pData[1] = pData[2]; pData[2] = tmp;
            break;
         case 0: /* CBA */
            swap( pData[0], pData[2] );
            break;
         default:
            ASSERT( false );
         }

         if( Comparator::isCorrectOrder( pData[1], pData[0] ) )
         {
            if( Comparator::isCorrectOrder( pData[2], pData[1] ) )
            {
               swap( pData[0], pData[2] );
            }
            else
               swap( pData[0], pData[1] );
         }

         if( Comparator::isCorrectOrder( pData[2], pData[1] ) )
            swap( pData[1], pData[2] );

      }	break;
         
      default:
      {
         half = count / 2;
         
         sort( start, half );
         sort( start + half, count - half );

         merge( start, half, start + half, count - half );
      }
      }
   }
   
   bool runTaskSort( CWorkerThread* pThread, int start, int count ) {
      CTaskCompletion Completion;
      CSorterInternalTask Task( &Completion );
      int half;

      if( count < MinItemCountPerTask )
      {
         sort( start, count );
         return true;
      }

      half = count / 2;

      /* sort each half */
      Task.init( mpSorter, start, half, TaskType_Sort );
      pThread->PushTask( &Task );
      
      runTaskSort( pThread, start + half, count - half );
      
      pThread->WorkUntilDone( &Completion );
      
      /* merge result */
      merge( start, half, start + half, count - half );
      
      return true;
   }
   
   bool run( CWorkerThread* pThread ) {
      bool bOk;
      
      switch( mTaskType ) {
      case TaskType_Sort: {
            bOk = runTaskSort( pThread, mStart, mCount );
            IF_FAILED_ASSERT( bOk );
         }	break;
      default:
         ASSERT( false );
      }
      /* DON'T delete this;  : these tasks get created on stack */
      return true;
   }

   
public:
   ParallelSorter <T, Comparator, MinItemCountPerTask>* mpSorter;
   eTaskType mTaskType;
   int mStart;
   int mCount;
   int mMergeRet;
};


template <class T, class Comparator, int MinItemCountPerTask> void CSorter <T, Comparator, MinItemCountPerTask>::sort() {
   if( !m_Count )
      return;

   CTaskCompletion Completion;
   CWorkerThread* pThread;
   CSorterInternalTask <T, Comparator, MinItemCountPerTask> Task( &Completion );

   pThread = CWorkerThread::GetCurrent();
   
   Task.Init( this, 0, m_Count );
   
   pThread->PushTask( &Task );
   pThread->WorkUntilDone( &Completion );
}

template <class T, class Comparator, int MinItemCountPerTask> void CSorter <T, Comparator, MinItemCountPerTask>::sortSerial() {
   CSorterInternalTask <T, Comparator, MinItemCountPerTask> Task( NULL );

   Task.Init( this, 0, m_Count );
   Task.Sort( 0, m_Count );
}

#endif // _MINIPARALLEL_SORT_H_
