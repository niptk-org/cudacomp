/**
 * @file    cudacomp.c
 * @brief   CUDA functions wrapper
 *
 * Also uses MAGMA library
 *
 *
 *
 */




/* ================================================================== */
/* ================================================================== */
/*            MODULE INFO                                             */
/* ================================================================== */
/* ================================================================== */

// module default short name
// all CLI calls to this module functions will be <shortname>.<funcname>
// if set to "", then calls use <funcname>
#define MODULE_SHORTNAME_DEFAULT "cuda"

// Module short description
#define MODULE_DESCRIPTION       "CUDA wrapper"











// uncomment for test print statements to stdout
//#define _PRINT_TEST



/* =============================================================================================== */
/* =============================================================================================== */
/*                                        HEADER FILES                                             */
/* =============================================================================================== */
/* =============================================================================================== */

// include sem_timedwait
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE	200809L
#endif


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include <sched.h>
#include <signal.h>


#include <semaphore.h>

#ifdef __MACH__
#include <mach/mach_time.h>
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 0
static int clock_gettime(int clk_id, struct mach_timespec *t)
{
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    uint64_t time;
    time = mach_absolute_time();
    double nseconds = ((double)time * (double)timebase.numer) / ((
                          double)timebase.denom);
    double seconds = ((double)time * (double)timebase.numer) / ((
                         double)timebase.denom * 1e9);
    t->tv_sec = seconds;
    t->tv_nsec = nseconds;
    return 0;
}
#else
#include <time.h>
#endif




#include <sys/types.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/types.h>


#ifdef HAVE_CUDA

#include <cuda_runtime_api.h>
#include <cuda_runtime.h>
#include <cublas_v2.h>
#include <device_types.h>
#include <pthread.h>
#include <cusolverDn.h>

#endif


#ifdef HAVE_MAGMA

//#include "magma.h"
#include "magma_v2.h"
#include "magma_lapack.h"

#endif



#include "CommandLineInterface/CLIcore.h"
#include "COREMOD_memory/COREMOD_memory.h"
#include "COREMOD_iofits/COREMOD_iofits.h"
#include "COREMOD_arith/COREMOD_arith.h"
#include "COREMOD_tools/COREMOD_tools.h"
#include "cudacomp/cudacomp.h"

#include "linopt_imtools/linopt_imtools.h" // for testing



/* =============================================================================================== */
/* =============================================================================================== */
/*                                      DEFINES, MACROS                                            */
/* =============================================================================================== */
/* =============================================================================================== */

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

# ifdef _OPENMP
# include <omp.h>
#define OMP_NELEMENT_LIMIT 1000000
# endif

static inline int r32up(int x)
{
    return ((x + 31) / 32) * 32;
}



/* =============================================================================================== */
/* =============================================================================================== */
/*                                  GLOBAL DATA DECLARATION                                        */
/* =============================================================================================== */
/* =============================================================================================== */


int FORCESEMINIT = 1;


//extern struct DATA data;


extern pid_t CLIPID;


static struct timespec tnow;
static struct timespec tdiff;
static double tdiffv;

static int IDtimerinit = 0;
static long IDtiming = -1; // index to image where timing should be written





#ifdef HAVE_CUDA
static int deviceCount;

GPUMATMULTCONF
gpumatmultconf[20]; // supports up to 20 configurations per process

static cudaError_t error;
static cublasStatus_t stat;
static float cublasSgemv_alpha = 1.0;
static float cublasSgemv_beta  = 0.0;


#endif




// MAGMA global variables

#ifdef HAVE_MAGMA

static int INIT_MAGMA = 0;

// queue for default magma device
static magma_queue_t   magmaqueue;

static long MAGMAloop_iter = 0;

static double *magma_h_A;
static double *magma_d_A;
static double *magma_d_AtA;
static double *magma_h_AtA;
static double *magma_w1; // eigenvalues
static double *magma_h_R;
static double *magma_h_work;
static double *magma_d_VT1;
static double *magma_h_VT1;
static double *magma_d_M2;
static double *magma_d_Ainv;
static double *magma_h_Ainv;
static double *magma_h_M2;
//static double *magma_h_S; //singular values
//static double *magma_d_U; //left singular vectors
//static double *magma_d_VT; //right singular vectors
//static double *magma_d_B;


static float *magmaf_h_A;
static float *magmaf_d_A;
static float *magmaf_d_AtA;
static float *magmaf_h_AtA;
static float *magmaf_w1; // eigenvalues
static float *magmaf_h_R;
static float *magmaf_h_work;
static float *magmaf_d_VT1;
static float *magmaf_h_VT1;
static float *magmaf_d_M2;
static float *magmaf_d_Ainv;
static float *magmaf_h_Ainv;
static float *magmaf_h_M2;
//static float *magmaf_h_S; //singular values
//static float *magmaf_d_U; //left singular vectors
//static float *magmaf_d_VT; //right singular vectors
//static float *magmaf_d_B;


static magma_int_t magma_aux_iwork[1];
static magma_int_t magma_lwork, magma_liwork;
static magma_int_t *magma_iwork;


#endif


#ifdef HAVE_QDWHpartial
int QDWHpartial(int M, int N,
                int fact,
                int psinv,
                double s,
                float  tol,
                float *d_A,  int ldda,
                float *S,
                float *d_U,  int lddu,
                float *d_VT, int lddvt,
                float *d_B,  int lddb,
                float *A, int lda,
                int *sizeS,
                int *sizeK,
                int *it,
                float *flops,
                magma_queue_t queue,
                cublasHandle_t handle);
#endif








/* ================================================================== */
/* ================================================================== */
/*            INITIALIZE LIBRARY                                      */
/* ================================================================== */
/* ================================================================== */

// Module initialization macro in CLIcore.h
// macro argument defines module name for bindings
//
INIT_MODULE_LIB(cudacomp)


static void __attribute__((constructor)) libinit_cudacomp_printinfo()
{
#ifdef HAVE_CUDA
    printf("[CUDA]");
#endif

#ifdef HAVE_MAGMA
    printf("[MAGMA]");
#endif
}




/* ================================================================== */
/* ================================================================== */
/*            COMMAND LINE INTERFACE (CLI) FUNCTIONS                  */
/* ================================================================== */
/* ================================================================== */


/** @name CLI bindings */

/* =============================================================================================== */
/* =============================================================================================== */
/*                                                                                                 */
/* 1. INITIALIZATION                                                                               */
/*                                                                                                 */
/* =============================================================================================== */
/* =============================================================================================== */


errno_t CUDACOMP_test_cli()
{
#ifdef HAVE_CUDA
    if(
        CLI_checkarg(1, 2) +
        CLI_checkarg(2, 2) +
        CLI_checkarg(3, 2) +
        CLI_checkarg(4, 2)
        == 0)
    {
        GPUcomp_test(data.cmdargtoken[1].val.numl, data.cmdargtoken[2].val.numl,
                     data.cmdargtoken[3].val.numl, data.cmdargtoken[4].val.numl);
        return CLICMD_SUCCESS;
    }
    else
    {
        return CLICMD_ERROR;
    }
#else
    printf("Error: function requires CUDA. Please install CUDA\n");

#endif
}





#ifdef HAVE_CUDA


/* =============================================================================================== */
/* =============================================================================================== */
/*                                                                                                 */
/* 2. LOW-LEVEL MATRIX VECTOR MULTIPLICATION FUNCTIONS                                             */
/*                                                                                                 */
/* =============================================================================================== */
/* =============================================================================================== */





/* =============================================================================================== */
/* =============================================================================================== */
/*                                                                                                 */
/* 3. SINGULAR VALUE DECOMPOSITION, PSEUDO-INVERSE                                                 */
/*                                                                                                 */
/* =============================================================================================== */
/* =============================================================================================== */


errno_t CUDACOMP_MatMatMult_testPseudoInverse_cli()
{
    if(
        CLI_checkarg(1, 4) +
        CLI_checkarg(2, 4) +
        CLI_checkarg(3, 3)
        == 0)
    {
        CUDACOMP_MatMatMult_testPseudoInverse(
            data.cmdargtoken[1].val.string,
            data.cmdargtoken[2].val.string,
            data.cmdargtoken[3].val.string
        );

        return CLICMD_SUCCESS;
    }
    else
    {
        return CLICMD_INVALID_ARG;
    }
}


errno_t CUDACOMP_magma_compute_SVDpseudoInverse_SVD_cli()
{
    if(
        CLI_checkarg(1, 4) +
        CLI_checkarg(2, 3) +
        CLI_checkarg(3, 1) +
        CLI_checkarg(4, 2) +
        CLI_checkarg(5, 3)
        == 0)
    {
        CUDACOMP_magma_compute_SVDpseudoInverse_SVD(
            data.cmdargtoken[1].val.string,
            data.cmdargtoken[2].val.string,
            data.cmdargtoken[3].val.numf,
            data.cmdargtoken[4].val.numl,
            data.cmdargtoken[5].val.string
        );

        return CLICMD_SUCCESS;
    }
    else
    {
        return CLICMD_INVALID_ARG;
    }
}



errno_t CUDACOMP_magma_compute_SVDpseudoInverse_cli()
{
    if(
        CLI_checkarg(1, 4) +
        CLI_checkarg(2, 3) +
        CLI_checkarg(3, 1) +
        CLI_checkarg(4, 2) +
        CLI_checkarg(5, 3) +
        CLI_checkarg(6, 2) +
        CLI_checkarg(7, 1) +
        CLI_checkarg(8, 1)
        == 0)
    {
        CUDACOMP_magma_compute_SVDpseudoInverse(
            data.cmdargtoken[1].val.string,
            data.cmdargtoken[2].val.string,
            data.cmdargtoken[3].val.numf,
            data.cmdargtoken[4].val.numl,
            data.cmdargtoken[5].val.string,
            0,
            data.cmdargtoken[6].val.numl,
            data.cmdargtoken[7].val.numf,
            data.cmdargtoken[8].val.numf,
            0);

        return CLICMD_SUCCESS;
    }
    else
    {
        return CLICMD_INVALID_ARG;
    }
}






/* =============================================================================================== */
/* =============================================================================================== */
/*                                                                                                 */
/* 4. HIGH LEVEL FUNCTIONS                                                                         */
/*                                                                                                 */
/* =============================================================================================== */
/* =============================================================================================== */


errno_t CUDACOMP_Coeff2Map_Loop_cli()
{
    if(
        CLI_checkarg(1, 4) +
        CLI_checkarg(2, 4) +
        CLI_checkarg(3, 2) +
        CLI_checkarg(4, 4)
        == 0)
    {
        CUDACOMP_Coeff2Map_Loop(
            data.cmdargtoken[1].val.string,
            data.cmdargtoken[2].val.string,
            data.cmdargtoken[3].val.numl,
            data.cmdargtoken[4].val.string,
            0,
            " "
        );

        return CLICMD_SUCCESS;
    }
    else
    {
        return CLICMD_INVALID_ARG;
    }
}


errno_t CUDACOMP_Coeff2Map_offset_Loop_cli()
{
    if(
        CLI_checkarg(1, 4) +
        CLI_checkarg(2, 4) +
        CLI_checkarg(3, 2) +
        CLI_checkarg(4, 4) +
        CLI_checkarg(5, 4)
        == 0)
    {
        CUDACOMP_Coeff2Map_Loop(
            data.cmdargtoken[1].val.string,
            data.cmdargtoken[2].val.string,
            data.cmdargtoken[3].val.numl,
            data.cmdargtoken[4].val.string,
            1,
            data.cmdargtoken[5].val.string
        );

        return CLICMD_SUCCESS;
    }
    else
    {
        return CLICMD_INVALID_ARG;
    }
}

/*
int_fast8_t CUDACOMP_MVMextractModesLoop_cli()
{
    if(CLI_checkarg(1,4)+CLI_checkarg(2,5)+CLI_checkarg(3,4)+CLI_checkarg(4,5)+CLI_checkarg(5,5)+CLI_checkarg(6,5)+CLI_checkarg(7,2)+CLI_checkarg(8,2)+CLI_checkarg(9,2)+CLI_checkarg(10,2)+CLI_checkarg(11,2)+CLI_checkarg(12,2)+CLI_checkarg(13,2)+CLI_checkarg(14,2)==0)
        CUDACOMP_MVMextractModesLoop(data.cmdargtoken[1].val.string, data.cmdargtoken[2].val.string, data.cmdargtoken[3].val.string, data.cmdargtoken[4].val.string, data.cmdargtoken[5].val.string, data.cmdargtoken[6].val.string, data.cmdargtoken[7].val.numl, data.cmdargtoken[8].val.numl, data.cmdargtoken[9].val.numl, data.cmdargtoken[10].val.numl, data.cmdargtoken[11].val.numl, data.cmdargtoken[12].val.numl, data.cmdargtoken[13].val.numl, data.cmdargtoken[14].val.numl);
    else
        return 1;
}
*/







errno_t CUDACOMP_MVMextractModesLoop_cli()
{

    // try FPS implementation
    // set data.fpsname, providing default value as first arg, and set data.FPS_CMDCODE value
    // default FPS name will be used if CLI process has NOT been named
    // see code in function_parameter.c for detailed rules
    function_parameter_getFPSname_from_CLIfunc("cudaMVM");

    if(data.FPS_CMDCODE != 0)   // use FPS implementation
    {
        // set pointers to CONF and RUN functions
        data.FPS_CONFfunc = CUDACOMP_MVMextractModesLoop_FPCONF;
        data.FPS_RUNfunc  = CUDACOMP_MVMextractModesLoop_RUN;
        function_parameter_execFPScmd();
        return RETURN_SUCCESS;
    }

    // non FPS implementation - all parameters specified at function launch
    if(
        CLI_checkarg(1, 4) +
        CLI_checkarg(2, 5) +
        CLI_checkarg(3, 4) +
        CLI_checkarg(4, 5) +
        CLI_checkarg(5, 5) +
        CLI_checkarg(6, 5) +
        CLI_checkarg(7, 2) +
        CLI_checkarg(8, 2) +
        CLI_checkarg(9, 2) +
        CLI_checkarg(10, 2) +
        CLI_checkarg(11, 2) +
        CLI_checkarg(12, 2) +
        CLI_checkarg(13, 2) +
        CLI_checkarg(14, 2)
        == 0)
    {
        CUDACOMP_MVMextractModesLoop(
            data.cmdargtoken[1].val.string,
            data.cmdargtoken[2].val.string,
            data.cmdargtoken[3].val.string,
            data.cmdargtoken[4].val.string,
            data.cmdargtoken[5].val.string,
            data.cmdargtoken[6].val.string,
            data.cmdargtoken[7].val.numl,
            data.cmdargtoken[8].val.numl,
            data.cmdargtoken[9].val.numl,
            data.cmdargtoken[10].val.numl,
            data.cmdargtoken[11].val.numl,
            data.cmdargtoken[12].val.numl,
            data.cmdargtoken[13].val.numl,
            data.cmdargtoken[14].val.numl
        );

        return RETURN_SUCCESS;
    }
    else
    {
        return RETURN_FAILURE;
    }
}









#endif



















/* =============================================================================================== */
/* =============================================================================================== */
/*                                    MODULE INITIALIZATION                                        */
/* =============================================================================================== */
/* =============================================================================================== */
/** @name Module initialization */



/* =============================================================================================== */
/* =============================================================================================== */
/*                                                                                                 */
/* 1. INITIALIZATION                                                                               */
/*                                                                                                 */
/* =============================================================================================== */
/* =============================================================================================== */



static errno_t init_module_CLI()
{
    long i;

#ifdef HAVE_CUDA
    //    printf("HAVE_CUDA defined\n");
    for(i = 0; i < 20; i++)
    {
        gpumatmultconf[i].init = 0;
        gpumatmultconf[i].alloc = 0;
    }
#endif

    //#ifndef HAVE_CUDA
    //printf("HAVE_CUDA NOT defined\n");
    //#endif


    /* =============================================================================================== */
    /* =============================================================================================== */
    /*                                                                                                 */
    /* 1. INITIALIZATION                                                                               */
    /*                                                                                                 */
    /* =============================================================================================== */
    /* =============================================================================================== */


#ifdef HAVE_CUDA
    strcpy(data.cmd[data.NBcmd].key, "cudacompinit");
    strcpy(data.cmd[data.NBcmd].module, __FILE__);
    data.cmd[data.NBcmd].fp = CUDACOMP_init;
    strcpy(data.cmd[data.NBcmd].info, "init CUDA comp");
    strcpy(data.cmd[data.NBcmd].syntax, "no argument");
    strcpy(data.cmd[data.NBcmd].example, "cudacompinit");
    strcpy(data.cmd[data.NBcmd].Ccall, "int CUDACOMP_init()");
    data.NBcmd++;

    strcpy(data.cmd[data.NBcmd].key, "cudacomptest");
    strcpy(data.cmd[data.NBcmd].module, __FILE__);
    data.cmd[data.NBcmd].fp = CUDACOMP_test_cli;
    strcpy(data.cmd[data.NBcmd].info, "test CUDA comp");
    strcpy(data.cmd[data.NBcmd].syntax,
           "<NB actuators [long]> <NB modes [long]> <NB pixels [long]> <NB GPU [long]>");
    strcpy(data.cmd[data.NBcmd].example, "cudacomptest 1000 20 1000 1");
    strcpy(data.cmd[data.NBcmd].Ccall,
           "int GPUcomp_test(long NBact, long NBmodes, long WFSsize, long GPUcnt)");
    data.NBcmd++;




    /* =============================================================================================== */
    /* =============================================================================================== */
    /*                                                                                                 */
    /* 2. LOW-LEVEL MATRIX VECTOR MULTIPLICATION FUNCTIONS                                             */
    /*                                                                                                 */
    /* =============================================================================================== */
    /* =============================================================================================== */





    /* =============================================================================================== */
    /* =============================================================================================== */
    /*                                                                                                 */
    /* 3. SINGULAR VALUE DECOMPOSITION, PSEUDO-INVERSE                                                 */
    /*                                                                                                 */
    /* =============================================================================================== */
    /* =============================================================================================== */


    RegisterCLIcommand(
        "cudatestpsinv",
        __FILE__,
        CUDACOMP_MatMatMult_testPseudoInverse_cli,
        "test pseudo inverse",
        "<matA> <matAinv> <matOut>",
        "cudatestpsinv matA matAinv matOut",
        "long CUDACOMP_MatMatMult_testPseudoInverse(const char *IDmatA_name, const char *IDmatAinv_name, const char *IDmatOut_name)");

    RegisterCLIcommand(
        "cudacomppsinvSVD",
        __FILE__,
        CUDACOMP_magma_compute_SVDpseudoInverse_SVD_cli,
        "compute pseudo inverse with direct SVD",
        "<input matrix [string]> <output pseudoinv [string]> <eps [float]> <NBmodes [long]> <VTmat [string]>",
        "cudacomppsinvSVD matA matAinv 0.01 100 VTmat",
        "int CUDACOMP_magma_compute_SVDpseudoInverse_SVD(const char *ID_Rmatrix_name, const char *ID_Cmatrix_name, double SVDeps, long MaxNBmodes, const char *ID_VTmatrix_name);");

    RegisterCLIcommand(
        "cudacomppsinv",
        __FILE__,
        CUDACOMP_magma_compute_SVDpseudoInverse_cli,
        "compute pseudo inverse",
        "<input matrix [string]> <output pseudoinv [string]> <eps [float]> <NBmodes [long]> <VTmat [string]>",
        "cudacomppsinv matA matAinv 0.01 100 VTmat 0 1e-4 1e-7",
        "int CUDACOMP_magma_compute_SVDpseudoInverse(const char *ID_Rmatrix_name, const char *ID_Cmatrix_name, double SVDeps, long MaxNBmodes, const char *ID_VTmatrix_name, int LOOPmode, int PSINV_MODE, double qdwh_s, float qdwh_tol)");






    /* =============================================================================================== */
    /* =============================================================================================== */
    /*                                                                                                 */
    /* 4. HIGH LEVEL FUNCTIONS                                                                         */
    /*                                                                                                 */
    /* =============================================================================================== */
    /* =============================================================================================== */


    RegisterCLIcommand(
        "cudacoeff2map",
        __FILE__,
        CUDACOMP_Coeff2Map_Loop_cli,
        "CUDA multiply vector by modes",
        "<modes> <coeffs vector> <GPU index [long]> <output map>",
        "cudacoeff2map modes coeff 4 outmap",
        "int CUDACOMP_Coeff2Map_Loop(const char *IDmodes_name, const char *IDcoeff_name, int GPUindex, const char *IDoutmap_name, int offsetmode, const char *IDoffset_name)");


    RegisterCLIcommand(
        "cudacoeffo2map",
        __FILE__,
        CUDACOMP_Coeff2Map_offset_Loop_cli,
        "CUDA multiply vector by modes and add offset",
        "<modes> <coeffs vector> <GPU index [long]> <output map> <offset image>",
        "cudacoeffo2map modes coeff 4 outmap offsetim",
        "int CUDACOMP_Coeff2Map_Loop(const char *IDmodes_name, const char *IDcoeff_name, int GPUindex, const char *IDoutmap_name, int offsetmode, const char *IDoffset_name)");


    RegisterCLIcommand(
        "cudaextrmodes",
        __FILE__,
        CUDACOMP_MVMextractModesLoop_cli,
        "CUDA extract mode values loop. Note that intot and refout parameters can be NULL",
        "<inval stream> <intot stream> <modes> <refin val> <refout_val> <outmode vals> <GPU index [long]> <PROCESS flag> <TRACEMODE flag> <MODE norm flag> <input semaphore> <axis orientation> <twait [us]> <semwarn>",
        "cudaextrmodes inmap inmaptot modes imref imoutref modeval 3 1 1 1 3 0 0",
        "int CUDACOMP_MVMextractModesLoop(const char *in_stream, const char *intot_stream, const char *IDmodes_name, const char *IDrefin_name, const char *IDrefout_name, const char *IDmodes_val_name, int GPUindex, int PROCESS, int TRACEMODE, int MODENORM, int insem, int axmode, long twait, int semwarn)");



#endif
    // add atexit functions here

    return RETURN_SUCCESS;
}










#ifdef HAVE_CUDA

int CUDACOMP_init()
{
    int device;
    struct cudaDeviceProp deviceProp;

    cudaGetDeviceCount(&deviceCount);
    printf("%d devices found\n", deviceCount);
    printf("\n");
    for(device = 0; device < deviceCount; ++device)
    {
        cudaGetDeviceProperties(&deviceProp, device);
        printf("Device %d [ %20s ]  has compute capability %d.%d.\n",
               device, deviceProp.name, deviceProp.major, deviceProp.minor);
        printf("  Total amount of global memory:                 %.0f MBytes (%llu bytes)\n",
               (float)deviceProp.totalGlobalMem / 1048576.0f,
               (unsigned long long) deviceProp.totalGlobalMem);
        printf("  (%2d) Multiprocessors\n", deviceProp.multiProcessorCount);
        printf("  GPU Clock rate:                                %.0f MHz (%0.2f GHz)\n",
               deviceProp.clockRate * 1e-3f, deviceProp.clockRate * 1e-6f);
        printf("\n");
#ifdef HAVE_MAGMA
        printf("Using MAGMA library\n");
        magma_print_environment();
#endif

        printf("\n");
    }

    return((int) deviceCount);
}







errno_t CUDACOMP_printGPUMATMULTCONF(int index)
{
    printf("\n");
    printf("============= GPUMATMULTCONF %d ======================\n", index);
    printf(" init              = %20d\n", (int) gpumatmultconf[index].init);
    printf(" refWFSinit        = %p\n", (void *) gpumatmultconf[index].refWFSinit);

    if(gpumatmultconf[index].refWFSinit != NULL)
    {
        printf("     refWFSinit[0]     = %20d\n",
               (int) gpumatmultconf[index].refWFSinit[0]);
    }

    printf(" alloc             = %20d\n", (int) gpumatmultconf[index].alloc);
    printf(" CM_ID             = %20ld\n", gpumatmultconf[index].CM_ID);
    printf(" CM_cnt            = %20ld\n", gpumatmultconf[index].CM_cnt);
    printf(" timerID           = %20ld\n", gpumatmultconf[index].timerID);
    printf(" M                 = %20d\n", (int) gpumatmultconf[index].M);
    printf(" N                 = %20d\n", (int) gpumatmultconf[index].N);

    /// synchronization
    printf(" sem               = %20d\n", (int) gpumatmultconf[index].sem);
    printf(" gpuinit           = %20d\n", (int) gpumatmultconf[index].gpuinit);

    /// one semaphore per thread
    /*
        sem_t **semptr1;
        sem_t **semptr2;
        sem_t **semptr3;
        sem_t **semptr4;
        sem_t **semptr5;
    */

    printf(" cMat              = %20p\n", (void *) gpumatmultconf[index].cMat);
    printf(" cMat_part         = %20p\n", (void *) gpumatmultconf[index].cMat_part);
    printf(" wfsVec            = %20p\n", (void *) gpumatmultconf[index].wfsVec);
    printf(" wfsVec_part       = %20p\n",
           (void *) gpumatmultconf[index].wfsVec_part);
    printf(" wfsRef            = %20p\n", (void *) gpumatmultconf[index].wfsRef);
    printf(" wfsRef_part       = %20p\n",
           (void *) gpumatmultconf[index].wfsRef_part);
    printf(" dmVec             = %20p\n", (void *) gpumatmultconf[index].dmVec);
    printf(" dmVecTMP          = %20p\n", (void *) gpumatmultconf[index].dmVecTMP);
    printf(" dmVec_part        = %20p\n",
           (void *) gpumatmultconf[index].dmVec_part);
    printf(" dmRef_part        = %20p\n",
           (void *) gpumatmultconf[index].dmRef_part);



    printf(" d_cMat            = %20p\n", (void *) gpumatmultconf[index].d_cMat);
    printf(" d_wfsVec          = %20p\n", (void *) gpumatmultconf[index].d_wfsVec);
    printf(" d_dmVec           = %20p\n", (void *) gpumatmultconf[index].d_dmVec);
    printf(" d_wfsRef          = %20p\n", (void *) gpumatmultconf[index].d_wfsRef);
    printf(" d_dmRef           = %20p\n", (void *) gpumatmultconf[index].d_dmRef);


    // threads
    printf(" thdata            = %20p\n", (void *) gpumatmultconf[index].thdata);
    printf(" threadarray       = %20p\n",
           (void *) gpumatmultconf[index].threadarray);
    printf(" NBstreams         = %20d\n", (int) gpumatmultconf[index].NBstreams);
    printf(" stream            = %20p\n", (void *) gpumatmultconf[index].stream);
    printf(" handle            = %20p\n", (void *) gpumatmultconf[index].handle);


    printf(" Nsize             = %20p\n", (void *) gpumatmultconf[index].Nsize);
    printf(" Noffset           = %20p\n", (void *) gpumatmultconf[index].Noffset);
    printf(" GPUdevice         = %20p\n", (void *) gpumatmultconf[index].GPUdevice);

    printf(" orientation       = %20d\n", (int) gpumatmultconf[index].orientation);

    printf("======================================================\n");
    printf("\n");

    return RETURN_SUCCESS;
}










errno_t GPUcomp_test(
    __attribute__((unused)) long NBact,
    long NBmodes,
    long WFSsize,
    long GPUcnt
)
{
    imageID     ID_contrM;
    imageID     ID_WFS;
    imageID     ID_cmd_modes;
    uint32_t   *cmsize;
    uint32_t   *wfssize;
    uint32_t   *cmdmodessize;
    int_fast8_t status;
    int_fast8_t GPUstatus[100];
    long        iter;
    long        NBiter = 50000;
    double      time1sec, time2sec;
    struct timespec tnow;
    int        *GPUdevices;
    double      SVDeps = 0.1;




    //printf("Testing SVD on CPU\n");
    // linopt_compute_reconstructionMatrix("Rmat", "Cmat", SVDeps, "VTmat");

    create_2Dimage_ID("Rmat", WFSsize, WFSsize);

    printf("Testing SVD on GPU\n");
    GPU_SVD_computeControlMatrix(0, "Rmat", "Cmat", SVDeps, "VTmat");
    list_image_ID();
    printf("DONE ... ");
    fflush(stdout);


    // CHECK RESULT
    /*   arraysizetmp = (long*) malloc(sizeof(long)*3);
       ID_R = image_ID("Rmat");
       ID_C = image_ID("Cmat");

       if(data.image[ID_R].md[0].naxis==3)
       {
           m = data.image[ID_R].md[0].size[0]*data.image[ID_R].md[0].size[1];
           n = data.image[ID_R].md[0].size[2];
           printf("3D image -> %ld %ld\n", m, n);
           fflush(stdout);
       }
       else
       {
           m = data.image[ID_R].md[0].size[0];
           n = data.image[ID_R].md[0].size[1];
           printf("2D image -> %ld %ld\n", m, n);
           fflush(stdout);
       }


       printf("CHECKING RESULT ... ");
       fflush(stdout);

       ID = create_2Dimage_ID("SVDcheck", n, n);
       for(ii=0;ii<n;ii++)
           for(jj=0;jj<n;jj++)
               {
                   val = 0.0;
                   for(k=0;k<m;k++)
                       val += data.image[ID_C].array.F[ii*m+k] * data.image[ID_R].array.F[jj*m+k];
                   data.image[ID].array.F[jj*n+ii] = val;
               }
       save_fits("SVDcheck", "!SVDcheck.fits");

    free(arraysizetmp);
       printf("DONE\n");
       fflush(stdout);*/


    printf("Testing GPU matrix multiplication speed, %ld GPUs\n", GPUcnt);

    GPUdevices = (int *) malloc(sizeof(int) * GPUcnt);
    for(int k = 0; k < GPUcnt; k++)
    {
        GPUdevices[k] = k + 8;
    }


    cmsize = (uint32_t *) malloc(sizeof(uint32_t) * 3);
    cmsize[0] = WFSsize;
    cmsize[1] = WFSsize;
    cmsize[2] = NBmodes;
    ID_contrM = create_image_ID("cudatestcm", 3, cmsize, _DATATYPE_FLOAT, 1, 0);

    wfssize = (uint32_t *) malloc(sizeof(uint32_t) * 2);
    wfssize[0] = WFSsize;
    wfssize[1] = WFSsize;
    ID_WFS = create_image_ID("cudatestwfs", 2, wfssize, _DATATYPE_FLOAT, 1, 0);

    cmdmodessize = (uint32_t *) malloc(sizeof(uint32_t) * 2);
    cmdmodessize[0] = NBmodes;
    cmdmodessize[1] = 1;
    ID_cmd_modes = create_image_ID("cudatestcmd", 2, cmdmodessize, _DATATYPE_FLOAT,
                                   1, 0);

    GPU_loop_MultMat_setup(0, data.image[ID_contrM].name, data.image[ID_WFS].name,
                           data.image[ID_cmd_modes].name, GPUcnt, GPUdevices, 0, 1, 1, 0);

    clock_gettime(CLOCK_REALTIME, &tnow);
    time1sec = 1.0 * ((long) tnow.tv_sec) + 1.0e-9 * tnow.tv_nsec;

    for(iter = 0; iter < NBiter; iter++)
    {
        status = 0;
        GPU_loop_MultMat_execute(0, &status, &GPUstatus[0], 1.0, 0.0, 1, 0);
    }
    clock_gettime(CLOCK_REALTIME, &tnow);
    time2sec = 1.0 * ((long) tnow.tv_sec) + 1.0e-9 * tnow.tv_nsec;

    printf("Frequ = %12.3f Hz\n", 1.0 * NBiter / (time2sec - time1sec));

    printf("done\n");
    fflush(stdout);

    delete_image_ID("cudatestcm");
    delete_image_ID("cudatestwfs");
    delete_image_ID("cudatestcmd");

    free(cmsize);
    free(wfssize);
    free(cmdmodessize);
    free(GPUdevices);


    return RETURN_SUCCESS;
}












/* =============================================================================================== */
/* =============================================================================================== */
/*                                                                                                 */
/* 2. LOW-LEVEL MATRIX VECTOR MULTIPLICATION FUNCTIONS                                             */
/*                                                                                                 */
/* =============================================================================================== */
/* =============================================================================================== */








void matrixMulCPU(
    float *cMat,
    float *wfsVec,
    float *dmVec,
    int    M,
    int    N
)
{
    printf("Conventional mat mult %d %d\n", M, N);
    for(int m = 0; m < M; m++)
    {
        dmVec[m] = 0.0;
        for(int n = 0; n < N; n++)
        {
            int index = m * N + n;
            dmVec[m] += cMat[index] * wfsVec[n];
        }
        //cMat[n*M+m]*wfsVec[n];
    }

    printf("cMat  : ");
    for(int i = 0; i < 5; i++)
    {
        printf("%f ", cMat[i]);
    }
    printf(" ... ");
    for(int i = N * M - 5; i < N * M; i++)
    {
        printf("%f ", cMat[i]);
    }
    printf("\n");

    printf("wfsVec: ");
    for(int n = 0; n < 5; n++)
    {
        printf("%f ", wfsVec[n]);
    }
    printf(" ... ");
    for(int n = N - 5; n < N; n++)
    {
        printf("%f ", wfsVec[n]);
    }
    printf("\n");

}








/*
 *
 * sequence of events :
 *
 * wait semptr1              (wait for input image data)
 * transfer input CPU -> GPU
 * post semptr2
 * COMPUTE
 * post semptr3
 * wait semptr4
 *
 *
 *
 *
 */


void __attribute__((hot)) *GPUcomputeMVM_function(
    void *ptr
)
{
    THDATA     *thdata;
    int         device;
    int         index;
    const char *ptr0; // source
    //const char *ptr1; // dest
    //float      *ptr0f; // test
    int        *ptrstat;
    //imageID     IDtest;
    //char        fname[200];
    long long   iter;
    long long   itermax = 1;
    //float       imtot;
    //float       alphatmp;
    //float       betatmp;
    int         semval;
    long        cnt;
    //FILE        *fptest;

    float alpharef, betaref;

    struct timespec t00;


    int ComputeGPU_FLAG = 1; //TEST

    thdata = (THDATA *) ptr;
    device = thdata->thread_no;
    index = thdata->cindex;

    ptrstat = (int *)((char *) thdata->status + sizeof(int) *
                      device); // + sizeof(int)*10*index);  //TBR

    *ptrstat = 1;


    // LOG function start
    int logfunc_level = 0;
    int logfunc_level_max = 1;
    char commentstring[200];
    sprintf(commentstring, "MVM compute on GPU");
    CORE_logFunctionCall(logfunc_level, logfunc_level_max, 0, __FILE__, __func__,
                         __LINE__, commentstring);



    ptr0 = (char *) gpumatmultconf[index].wfsVec;
    ptr0 += sizeof(float) * gpumatmultconf[index].Noffset[device];
    //ptr0f = (float*) ptr0;


    cudaSetDevice(gpumatmultconf[index].GPUdevice[device]);

    cublasSetStream(gpumatmultconf[index].handle[device],
                    gpumatmultconf[index].stream[device]);



    if(gpumatmultconf[index].sem == 1)
    {
        itermax = -1;
    }
    else
    {
        itermax = 1;
    }





    iter = 0;
    while(iter != itermax)
    {
        //printf("====================================== gpumatmultconf[index].M = %d\n", gpumatmultconf[index].M);
        //fflush(stdout);


        clock_gettime(CLOCK_REALTIME, &t00);

        // copy DM reference to output to prepare computation:   d_dmVec <- d_dmRef
        if(ComputeGPU_FLAG == 1)
        {
            error = cudaMemcpy(
                        gpumatmultconf[index].d_dmVec[device],
                        gpumatmultconf[index].d_dmRef[device],
                        sizeof(float) * gpumatmultconf[index].M,
                        cudaMemcpyDeviceToDevice);

            if(error != cudaSuccess)
            {
                printf("cudaMemcpy d_wfsVec wfsVec returned error code %d, line(%d)\n", error,
                       __LINE__);
                fflush(stdout);
                exit(EXIT_FAILURE);
            }
        }

        *ptrstat = 2; // wait for image

        //
        // Wait for semaphore #1 to be posted to transfer from CPU to GPU
        //
        //printf("%s %d      index = %d  sem = %d\n", __FILE__, __LINE__, index, gpumatmultconf[index].sem);//TEST
        if(gpumatmultconf[index].sem == 1)
        {
            sem_wait(gpumatmultconf[index].semptr1[device]);

            if(FORCESEMINIT == 1)
            {
                sem_getvalue(gpumatmultconf[index].semptr1[device], &semval);
                for(cnt = 0; cnt < semval; cnt++)
                {
                    printf("WARNING %s %d  : sem_trywait on semptr1 index %d device %d\n", __FILE__,
                           __LINE__, index, device);
                    fflush(stdout);
                    sem_trywait(gpumatmultconf[index].semptr1[device]);
                }
            }
        }


        thdata->t0->tv_sec = t00.tv_sec;
        thdata->t0->tv_nsec = t00.tv_nsec;
        clock_gettime(CLOCK_REALTIME, thdata->t1);

        *ptrstat = 3; // transfer: prt0 -> d_wfsVec
        if(ComputeGPU_FLAG == 1)
        {
            stat = cublasSetVector(gpumatmultconf[index].Nsize[device], sizeof(float),
                                   (float *) ptr0, 1, gpumatmultconf[index].d_wfsVec[device], 1);
            if(stat != CUBLAS_STATUS_SUCCESS)
            {
                fprintf(stderr, "!!!! device access error (read C)\n");
                if(stat == CUBLAS_STATUS_NOT_INITIALIZED)
                {
                    printf("   CUBLAS_STATUS_NOT_INITIALIZED\n");
                }
                if(stat == CUBLAS_STATUS_INVALID_VALUE)
                {
                    printf("   CUBLAS_STATUS_INVALID_VALUE\n");
                }
                if(stat == CUBLAS_STATUS_MAPPING_ERROR)
                {
                    printf("   CUBLAS_STATUS_MAPPING_ERROR\n");
                }
                exit(EXIT_FAILURE);
            }
        }

        clock_gettime(CLOCK_REALTIME, thdata->t2);

        if(gpumatmultconf[index].refWFSinit[device] ==
                0) // compute DM reference (used when reference changes)
        {
            printf("DM reference changed -> recompute\n");
            fflush(stdout);

            *ptrstat = 4; // compute

            // enable this post if outside process needs to be notified of computation start
            /*            if(gpumatmultconf[index].sem==1)
                            sem_post(gpumatmultconf[index].semptr2[device]);*/


            //  printf("%d  device %d (GPU %d): compute reference product\n", index, device, gpumatmultconf[index].GPUdevice[device]);
            //  fflush(stdout);

            //            alphatmp = cublasSgemv_alpha;
            //            betatmp = cublasSgemv_beta;

            // MOVE THIS TO CPU AS A SEPARATE THREAD TO AVOID LOOP PAUSE ??
            //        cublasSgemv_alpha = 1.0;
            //        cublasSgemv_beta = 0.0;
            alpharef = 1.0;
            betaref = 0.0;

            stat = cublasSgemv(
                       gpumatmultconf[index].handle[device],
                       CUBLAS_OP_N,
                       gpumatmultconf[index].M,
                       gpumatmultconf[index].Nsize[device],
                       &alpharef,
                       gpumatmultconf[index].d_cMat[device],
                       gpumatmultconf[index].M,
                       gpumatmultconf[index].d_wfsVec[device],
                       1,
                       &betaref,
                       gpumatmultconf[index].d_dmRef[device],
                       1);


            if(stat != CUBLAS_STATUS_SUCCESS)
            {
                printf("cublasSgemv returned error code %d, line(%d)\n", stat, __LINE__);
                fflush(stdout);
                if(stat == CUBLAS_STATUS_NOT_INITIALIZED)
                {
                    printf("   CUBLAS_STATUS_NOT_INITIALIZED\n");
                }
                if(stat == CUBLAS_STATUS_INVALID_VALUE)
                {
                    printf("   CUBLAS_STATUS_INVALID_VALUE\n");
                }
                if(stat == CUBLAS_STATUS_ARCH_MISMATCH)
                {
                    printf("   CUBLAS_STATUS_ARCH_MISMATCH\n");
                }
                if(stat == CUBLAS_STATUS_EXECUTION_FAILED)
                {
                    printf("   CUBLAS_STATUS_EXECUTION_FAILED\n");
                }

                printf("device %d of index %d\n", device, index);
                printf("GPU device                          = %d\n",
                       gpumatmultconf[index].GPUdevice[device]);

                printf("CUBLAS_OP_N                         = %d\n", CUBLAS_OP_N);
                printf("alpha                               = %f\n", alpharef);
                printf("beta                                = %f\n", betaref);
                printf("gpumatmultconf[index].M             = %d\n",
                       (int) gpumatmultconf[index].M);
                printf("gpumatmultconf[index].Nsize[device] = %d\n",
                       (int) gpumatmultconf[index].Nsize[device]);
                fflush(stdout);
                exit(EXIT_FAILURE);
            }

            //          cublasSgemv_alpha = alphatmp;
            //          cublasSgemv_beta = betatmp;

            gpumatmultconf[index].refWFSinit[device] = 1;

            // enable this post if outside process needs to be notified of computation start
            /*
            if(gpumatmultconf[index].sem==1)
                sem_post(gpumatmultconf[index].semptr3[device]);
            */

            *ptrstat = 5; // transfer result

            if(gpumatmultconf[index].sem == 1)
            {
                sem_wait(gpumatmultconf[index].semptr4[device]);
                if(FORCESEMINIT == 1)
                {
                    sem_getvalue(gpumatmultconf[index].semptr4[device], &semval);
                    for(cnt = 0; cnt < semval; cnt++)
                    {
                        printf("WARNING %s %d  : sem_trywait on semptr4 index %d device %d\n", __FILE__,
                               __LINE__, index, device);
                        fflush(stdout);
                        sem_trywait(gpumatmultconf[index].semptr4[device]);
                    }
                }
            }


            // copy d_dmRef -> dmRef_part
            stat = cublasGetVector(
                       gpumatmultconf[index].M,
                       sizeof(float),
                       gpumatmultconf[index].d_dmRef[device],
                       1,
                       gpumatmultconf[index].dmRef_part[device],
                       1);

            if(stat != CUBLAS_STATUS_SUCCESS)
            {
                fprintf(stderr, "!!!! device access error (read C)\n");
                if(stat == CUBLAS_STATUS_NOT_INITIALIZED)
                {
                    printf("   CUBLAS_STATUS_NOT_INITIALIZED\n");
                }
                if(stat == CUBLAS_STATUS_INVALID_VALUE)
                {
                    printf("   CUBLAS_STATUS_INVALID_VALUE\n");
                }
                if(stat == CUBLAS_STATUS_MAPPING_ERROR)
                {
                    printf("   CUBLAS_STATUS_MAPPING_ERROR\n");
                }
                exit(EXIT_FAILURE);
            }

            // TEST

            /*    sprintf(fname, "gputest%d.txt", device);
                if((fptest = fopen(fname, "w"))==NULL)
                {
                    printf("ERROR: cannot create file \"%s\"\n", fname);
                    exit(0);
                }
                printf("Writing test file \"%s\"\n", fname);
                fflush(stdout);
                for(ii=0; ii<gpumatmultconf[index].M; ii++)
                    fprintf(fptest, "%ld %f\n", ii, gpumatmultconf[index].dmRef_part[device][ii]);
                fclose(fptest);
            */
            if(gpumatmultconf[index].sem == 1)
            {
                sem_post(gpumatmultconf[index].semptr5[device]);
            }

            *ptrstat = 6;
        }
        else
        {
            *ptrstat = 4; // compute

            //
            // Post semaphore #2 when starting computation
            // Enable if listening to semptr2
            /*
            if(gpumatmultconf[index].sem==1)
                sem_post(gpumatmultconf[index].semptr2[device]);
                */

            if(ComputeGPU_FLAG == 1)
            {
                stat = cublasSgemv(
                           gpumatmultconf[index].handle[device],
                           CUBLAS_OP_N,
                           gpumatmultconf[index].M,
                           gpumatmultconf[index].Nsize[device],
                           &cublasSgemv_alpha,
                           gpumatmultconf[index].d_cMat[device],
                           gpumatmultconf[index].M,
                           gpumatmultconf[index].d_wfsVec[device],
                           1,
                           &cublasSgemv_beta,
                           gpumatmultconf[index].d_dmVec[device],
                           1);


                if(stat != CUBLAS_STATUS_SUCCESS)
                {
                    printf("cublasSgemv returned error code %d, line(%d), index=%d\n", stat,
                           __LINE__, index);
                    fflush(stdout);
                    if(stat == CUBLAS_STATUS_NOT_INITIALIZED)
                    {
                        printf("   CUBLAS_STATUS_NOT_INITIALIZED\n");
                    }
                    if(stat == CUBLAS_STATUS_INVALID_VALUE)
                    {
                        printf("   CUBLAS_STATUS_INVALID_VALUE\n");
                    }
                    if(stat == CUBLAS_STATUS_ARCH_MISMATCH)
                    {
                        printf("   CUBLAS_STATUS_ARCH_MISMATCH\n");
                    }
                    if(stat == CUBLAS_STATUS_EXECUTION_FAILED)
                    {
                        printf("   CUBLAS_STATUS_EXECUTION_FAILED\n");
                    }

                    printf("device %d of index %d\n", device, index);
                    printf("GPU device                          = %d\n",
                           gpumatmultconf[index].GPUdevice[device]);
                    printf("CUBLAS_OP_N                         = %d\n", CUBLAS_OP_N);
                    printf("alpha                               = %f\n", cublasSgemv_alpha);
                    printf("alpha                               = %f\n", cublasSgemv_beta);
                    printf("gpumatmultconf[index].M             = %d\n",
                           (int) gpumatmultconf[index].M);
                    printf("gpumatmultconf[index].Nsize[device] = %d\n",
                           (int) gpumatmultconf[index].Nsize[device]);
                    fflush(stdout);
                    exit(EXIT_FAILURE);
                }
            }
            clock_gettime(CLOCK_REALTIME, thdata->t3);


            //
            // When computation is done on GPU, post semaphore #3
            //
            /*
            if(gpumatmultconf[index].sem==1)
                sem_post(gpumatmultconf[index].semptr3[device]);
            */
            *ptrstat = 5; // transfer result


            //
            // Wait for semaphore #4 to be posted to transfer from GPU to CPU
            //
            if(gpumatmultconf[index].sem == 1)
            {
                sem_wait(gpumatmultconf[index].semptr4[device]);
                if(FORCESEMINIT == 1)
                {
                    sem_getvalue(gpumatmultconf[index].semptr4[device], &semval);
                    for(cnt = 0; cnt < semval; cnt++)
                    {
                        printf("WARNING %s %d  : sem_trywait on semptr4 index %d device %d\n",
                               __FILE__, __LINE__, index, device);
                        fflush(stdout);
                        sem_trywait(gpumatmultconf[index].semptr4[device]);
                    }
                }
            }

            clock_gettime(CLOCK_REALTIME, thdata->t4);

            //cudaMemcpy ( gpumatmultconf[index].dmVec_part[device], gpumatmultconf[index].d_dmVec[device], sizeof(float)*gpumatmultconf[index].M, cudaMemcpyDeviceToHost);
            // result is on gpumatmultconf[index].d_dmVec[device]

            if(ComputeGPU_FLAG == 1)
            {
                stat = cublasGetVector(
                           gpumatmultconf[index].M,
                           sizeof(float),
                           gpumatmultconf[index].d_dmVec[device],
                           1,
                           gpumatmultconf[index].dmVec_part[device],
                           1);

                if(stat != CUBLAS_STATUS_SUCCESS)
                {
                    fprintf(stderr, "!!!! device access error (read C)\n");
                    if(stat == CUBLAS_STATUS_NOT_INITIALIZED)
                    {
                        printf("   CUBLAS_STATUS_NOT_INITIALIZED\n");
                    }
                    if(stat == CUBLAS_STATUS_INVALID_VALUE)
                    {
                        printf("   CUBLAS_STATUS_INVALID_VALUE\n");
                    }
                    if(stat == CUBLAS_STATUS_MAPPING_ERROR)
                    {
                        printf("   CUBLAS_STATUS_MAPPING_ERROR\n");
                    }
                    exit(EXIT_FAILURE);
                }
            }
        }

        clock_gettime(CLOCK_REALTIME, thdata->t5);
        //
        // When data is ready on CPU, post semaphore #5
        //
        if(gpumatmultconf[index].sem == 1)
        {
            sem_post(gpumatmultconf[index].semptr5[device]);
        }

        *ptrstat = 6;

        // START MODE VALUES COMPUTATION HERE



        iter++;
    }


    // LOG function / process end
    CORE_logFunctionCall(logfunc_level, logfunc_level_max, 1, __FILE__, __func__,
                         __LINE__, commentstring);

    pthread_exit(0);
}









errno_t GPUloadCmat(
    int index
)
{

    printf("LOADING MATRIX TO GPU ... ");
    fflush(stdout);

    for(int device = 0; device < gpumatmultconf[index].NBstreams; device++)
    {
        for(unsigned int n = gpumatmultconf[index].Noffset[device];
                n < gpumatmultconf[index].Noffset[device] + gpumatmultconf[index].Nsize[device];
                n++)
        {
            if(gpumatmultconf[index].orientation == 0)
            {
                for(unsigned int m = 0; m < gpumatmultconf[index].M; m++)
                {
                    gpumatmultconf[index].cMat_part[device][(n -
                                                            gpumatmultconf[index].Noffset[device])*gpumatmultconf[index].M + m] =
                                                                    gpumatmultconf[index].cMat[m * gpumatmultconf[index].N + n];
                }
            }
            else
            {
                for(unsigned int m = 0; m < gpumatmultconf[index].M; m++)
                {
                    gpumatmultconf[index].cMat_part[device][(n -
                                                            gpumatmultconf[index].Noffset[device])*gpumatmultconf[index].M + m] =
                                                                    gpumatmultconf[index].cMat[n * gpumatmultconf[index].M + m];
                }
            }
        }
    }

    for(int device = 0; device < gpumatmultconf[index].NBstreams; device++)
    {
        cudaSetDevice(gpumatmultconf[index].GPUdevice[device]);

        error = cublasSetMatrix(
                    gpumatmultconf[index].M,
                    gpumatmultconf[index].Nsize[device],
                    sizeof(float),
                    gpumatmultconf[index].cMat_part[device],
                    gpumatmultconf[index].M,
                    gpumatmultconf[index].d_cMat[device],
                    gpumatmultconf[index].M);

        if(error != cudaSuccess)
        {
            printf("cudblasSetMatrix returned error code %d, line(%d)\n", error, __LINE__);
            exit(EXIT_FAILURE);
        }
    }
    printf("done\n");
    fflush(stdout);

    return RETURN_SUCCESS;
}







void *GPU_scanDevices(void *deviceCount_void_ptr)
{
    int *devcnt_ptr = (int *) deviceCount_void_ptr;
    int deviceCount;
    int device;
    struct cudaDeviceProp deviceProp;

    printf("Scanning for GPU devices ...\n");
    fflush(stdout);

    cudaGetDeviceCount(&deviceCount);
    printf("%d devices found\n", deviceCount);
    fflush(stdout);

    printf("\n");
    for(device = 0; device < deviceCount; ++device)
    {
        cudaGetDeviceProperties(&deviceProp, device);
        printf("Device %d [ %20s ]  has compute capability %d.%d.\n",
               device, deviceProp.name, deviceProp.major, deviceProp.minor);

        printf("  Total amount of global memory:                 %.0f MBytes (%llu bytes)\n",
               (float)deviceProp.totalGlobalMem / 1048576.0f,
               (unsigned long long) deviceProp.totalGlobalMem);

        printf("  (%2d) Multiprocessors\n",
               deviceProp.multiProcessorCount);

        printf("  GPU Clock rate:                                %.0f MHz (%0.2f GHz)\n",
               deviceProp.clockRate * 1e-3f,
               deviceProp.clockRate * 1e-6f);

        printf("\n");
    }

    printf("Done scanning for GPU devices\n");
    fflush(stdout);

    *devcnt_ptr = deviceCount;

    return NULL;
}






/**
 * ## Purpose
 *
 * Setup matrix multiplication using multiple GPUs
 *
 * ## Parameters
 *
 * @param[in]	index
 * 		Configuration index
 *
 * @param[in]   IDcontrM_name
 * 		Control matrix image name
 *
 * @param[in]	IDwfsim_name
 * 		Wavefront sensor image stream
 *
 * @param[out]	IDoutdmmodes_name
 * 		Output DM modes
 *
 *  IDoutdmmodes_name  = alpha * IDcontrM_name x IDwfsim_name
 *
 * ## Details
 *
 * upon setup, IDwfsim_name is the WFS ref and initWFSref = 0
 *
*/

errno_t GPU_loop_MultMat_setup(
    int         index,
    const char *IDcontrM_name,
    const char *IDwfsim_name,
    const char *IDoutdmmodes_name,
    long        NBGPUs,
    int        *GPUdevice,
    int         orientation,
    int         USEsem,
    int         initWFSref,
    long        loopnb
)
{
    //CUDACOMP_printGPUMATMULTCONF(index);

    /// This function will not do anything if the initialization has already been performed

    if(gpumatmultconf[index].init == 0)
    {
        int pid;
        //struct cudaDeviceProp deviceProp;
        char sname[200];

        imageID IDcontrM;
        imageID IDwfsim;
        imageID IDwfsref;



        printf("STARTING SETUP %d .....\n", index);
        fflush(stdout);

        pid = getpid();

        if(IDtimerinit == 0)
        {
            char name[200];

            sprintf(name, "aol%ld_looptiming", loopnb);
            IDtiming = image_ID(name);

            if(IDtiming == -1)
            {
                IDtiming = create_2Dimage_ID(name, 50, 1);
            }
        }



        if(gpumatmultconf[index].alloc == 1)
        {
            GPU_loop_MultMat_free(index);
            gpumatmultconf[index].alloc = 0;
        }

        if(USEsem == 1)
        {
            gpumatmultconf[index].sem = 1;
        }
        else
        {
            gpumatmultconf[index].sem = 0;
        }

        printf("USEsem = %d\n", USEsem);
        fflush(stdout);



        gpumatmultconf[index].orientation = orientation;

        printf("input CM name : %s\n", IDcontrM_name);
        fflush(stdout);
        //sleep(2);
        gpumatmultconf[index].CM_ID = image_ID(IDcontrM_name);

        printf("CM_ID = %ld\n", gpumatmultconf[index].CM_ID);
        fflush(stdout);
        //	sleep(2);

        gpumatmultconf[index].CM_cnt =
            data.image[gpumatmultconf[index].CM_ID].md[0].cnt0;



        /// Load Control Matrix
        IDcontrM = image_ID(IDcontrM_name);
        //		printf("control matrix loaded: IDcontrM = %ld\n", IDcontrM);
        //        fflush(stdout);
        //	sleep(2);


        if(orientation == 0)
        {
            if(data.image[IDcontrM].md[0].naxis == 3)
            {
                gpumatmultconf[index].M = data.image[IDcontrM].md[0].size[2];
                gpumatmultconf[index].N = data.image[IDcontrM].md[0].size[0] *
                                          data.image[IDcontrM].md[0].size[1];
                //   cmatdim = 3;
            }
            else
            {
                gpumatmultconf[index].M = data.image[IDcontrM].md[0].size[1];
                gpumatmultconf[index].N = data.image[IDcontrM].md[0].size[0];
                // cmatdim = 2;
            }
            printf("[0] [%ld] M = %d\n", IDcontrM, (int) gpumatmultconf[index].M);
            printf("[0] [%ld] N = %d\n", IDcontrM, (int) gpumatmultconf[index].N);
        }
        else
        {
            if(data.image[IDcontrM].md[0].naxis == 3)
            {
                gpumatmultconf[index].M = data.image[IDcontrM].md[0].size[0] *
                                          data.image[IDcontrM].md[0].size[1];
                gpumatmultconf[index].N = data.image[IDcontrM].md[0].size[2];
                //   cmatdim = 3;
            }
            else
            {
                gpumatmultconf[index].M = data.image[IDcontrM].md[0].size[0];
                gpumatmultconf[index].N = data.image[IDcontrM].md[0].size[1];
                //   cmatdim = 2;
            }

            printf("[1] [%ld] M = %d\n", IDcontrM, (int) gpumatmultconf[index].M);
            printf("[1] [%ld] N = %d\n", IDcontrM, (int) gpumatmultconf[index].N);
        }

        gpumatmultconf[index].cMat =  data.image[IDcontrM].array.F;


        /// Load Input vectors
        IDwfsim = image_ID(IDwfsim_name);
        gpumatmultconf[index].wfsVec = data.image[IDwfsim].array.F;
        IDwfsref = image_ID(IDwfsim_name);
        gpumatmultconf[index].wfsRef = data.image[IDwfsref].array.F;

        if(orientation == 0)
        {
            printf("[0] Input vector size: %ld %ld\n",
                   (long) data.image[IDwfsim].md[0].size[0],
                   (long) data.image[IDwfsim].md[0].size[1]);

            if((uint32_t)(
                        data.image[IDwfsim].md[0].size[0]*data.image[IDwfsim].md[0].size[1]) !=
                    gpumatmultconf[index].N)
            {
                printf("ERROR: CONTRmat and WFSvec size not compatible: %ld %d\n",
                       (long)(data.image[IDwfsim].md[0].size[0]*data.image[IDwfsim].md[0].size[1]),
                       (int) gpumatmultconf[index].N);
                fflush(stdout);
                sleep(2);
                exit(0);
            }
        }
        else
        {
            printf("[1] Input vector size: %ld \n",
                   (long) data.image[IDwfsim].md[0].size[0]);
            if(data.image[IDwfsim].md[0].size[0] != gpumatmultconf[index].N)
            {
                printf("ERROR: CONTRmat and WFSvec size not compatible: %ld %d\n",
                       (long) data.image[IDwfsim].md[0].size[0], (int) gpumatmultconf[index].N);
                fflush(stdout);
                sleep(2);
                exit(0);
            }
        }


        printf("Setting up gpumatmultconf\n");
        fflush(stdout);

        if((gpumatmultconf[index].IDout = image_ID(IDoutdmmodes_name)) == -1)
        {
            uint32_t *sizearraytmp;

            sizearraytmp = (uint32_t *) malloc(sizeof(uint32_t) * 2);
            sizearraytmp[0] = gpumatmultconf[index].M;
            sizearraytmp[1] = 1;
            gpumatmultconf[index].IDout = create_image_ID(IDoutdmmodes_name, 2,
                                          sizearraytmp, _DATATYPE_FLOAT, 1, 10);
            free(sizearraytmp);
        }
        else
        {
            if((uint32_t)(data.image[gpumatmultconf[index].IDout].md[0].size[0] *
                          data.image[gpumatmultconf[index].IDout].md[0].size[1]) !=
                    gpumatmultconf[index].M)
            {
                printf("ERROR: CONTRmat and WFSvec size not compatible: %ld %d\n",
                       (long)(data.image[gpumatmultconf[index].IDout].md[0].size[0] *
                              data.image[gpumatmultconf[index].IDout].md[0].size[1]),
                       (int) gpumatmultconf[index].M);
                printf("gpumatmultconf[index].IDout = %ld\n", gpumatmultconf[index].IDout);
                list_image_ID();
                fflush(stdout);
                sleep(2);
                exit(0);
            }
        }

        gpumatmultconf[index].dmVecTMP =
            data.image[gpumatmultconf[index].IDout].array.F;





        // This section will create a thread


        pthread_t GPUscan_thread;

        pthread_create(&GPUscan_thread, NULL, GPU_scanDevices, (void *) &deviceCount);
        if(pthread_join(GPUscan_thread, NULL))
        {
            fprintf(stderr, "Error joining thread\n");
            exit(0);
        }


        gpumatmultconf[index].NBstreams = deviceCount;
        if(NBGPUs < deviceCount)
        {
            gpumatmultconf[index].NBstreams = NBGPUs;
        }


        gpumatmultconf[index].Nsize = (uint_fast32_t *) malloc(sizeof(
                                          long) * gpumatmultconf[index].NBstreams);
        gpumatmultconf[index].Noffset = (uint_fast32_t *) malloc(sizeof(
                                            long) * gpumatmultconf[index].NBstreams);
        gpumatmultconf[index].Noffset[0] = 0;
        for(int device = 1; device < gpumatmultconf[index].NBstreams; device++)
        {
            gpumatmultconf[index].Noffset[device] = gpumatmultconf[index].Noffset[device -
                                                    1] + (long)(gpumatmultconf[index].N / gpumatmultconf[index].NBstreams);
            gpumatmultconf[index].Nsize[device - 1] = gpumatmultconf[index].Noffset[device]
                    - gpumatmultconf[index].Noffset[device - 1];
        }
        gpumatmultconf[index].Nsize[gpumatmultconf[index].NBstreams - 1] =
            gpumatmultconf[index].N -
            gpumatmultconf[index].Noffset[gpumatmultconf[index].NBstreams - 1];


        printf("Allocating physical GPU(s) to stream(s) (index %d, NBGPU(s) = %ld)\n",
               index, NBGPUs);
        printf("%d stream(s)\n", gpumatmultconf[index].NBstreams);
        fflush(stdout);

        gpumatmultconf[index].GPUdevice = (int *) malloc(sizeof(int) * NBGPUs);

        printf("- - - - - - - - -\n");
        fflush(stdout);

        for(int device = 0; device < gpumatmultconf[index].NBstreams; device++)
        {
            printf("stream %2d  ->  GPU device %2d\n", device, GPUdevice[device]);
            fflush(stdout);
            gpumatmultconf[index].GPUdevice[device] = GPUdevice[device];
        }

        printf("-----------------------------------------------------\n");
        fflush(stdout);
        for(int device = 0; device < gpumatmultconf[index].NBstreams; device++)
        {
            printf("DEVICE %2d  [%2d]:  %5d -> %5d  (%d)\n", device,
                   gpumatmultconf[index].GPUdevice[device],
                   (int) gpumatmultconf[index].Noffset[device],
                   (int)(gpumatmultconf[index].Noffset[device] +
                         gpumatmultconf[index].Nsize[device]),
                   (int) gpumatmultconf[index].Nsize[device]);
            fflush(stdout);
        }
        printf("-----------------------------------------------------\n");
        fflush(stdout);



        // device (GPU)
        gpumatmultconf[index].d_cMat = (float **) malloc(sizeof(
                                           float *)*gpumatmultconf[index].NBstreams);
        if(gpumatmultconf[index].d_cMat == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }

        gpumatmultconf[index].d_wfsVec = (float **) malloc(sizeof(
                                             float *)*gpumatmultconf[index].NBstreams);
        if(gpumatmultconf[index].d_wfsVec == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }


        gpumatmultconf[index].d_dmVec = (float **) malloc(sizeof(
                                            float *)*gpumatmultconf[index].NBstreams);
        if(gpumatmultconf[index].d_dmVec == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }

        gpumatmultconf[index].d_wfsRef = (float **) malloc(sizeof(
                                             float *)*gpumatmultconf[index].NBstreams); // WFS reference
        if(gpumatmultconf[index].d_wfsRef == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }

        gpumatmultconf[index].d_dmRef = (float **) malloc(sizeof(
                                            float *)*gpumatmultconf[index].NBstreams);  // DM reference
        if(gpumatmultconf[index].d_dmRef == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }

        gpumatmultconf[index].stream = (cudaStream_t *) malloc(sizeof(
                                           cudaStream_t) * gpumatmultconf[index].NBstreams);
        if(gpumatmultconf[index].stream == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }

        gpumatmultconf[index].handle = (cublasHandle_t *) malloc(sizeof(
                                           cublasHandle_t) * gpumatmultconf[index].NBstreams);
        if(gpumatmultconf[index].handle == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }


        // host (computer)
        gpumatmultconf[index].cMat_part = (float **) malloc(sizeof(
                                              float *)*gpumatmultconf[index].NBstreams);
        if(gpumatmultconf[index].cMat_part == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }

        gpumatmultconf[index].wfsVec_part = (float **) malloc(sizeof(
                                                float *)*gpumatmultconf[index].NBstreams);
        if(gpumatmultconf[index].wfsVec_part == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }

        gpumatmultconf[index].dmVec_part = (float **) malloc(sizeof(
                                               float *)*gpumatmultconf[index].NBstreams);
        if(gpumatmultconf[index].dmVec_part == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }

        gpumatmultconf[index].wfsRef_part = (float **) malloc(sizeof(
                                                float *)*gpumatmultconf[index].NBstreams); // WFS reference
        if(gpumatmultconf[index].wfsRef_part == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }

        gpumatmultconf[index].dmRef_part = (float **) malloc(sizeof(
                                               float *)*gpumatmultconf[index].NBstreams);  // DM reference (for checking only)
        if(gpumatmultconf[index].dmRef_part == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }

        gpumatmultconf[index].refWFSinit = (int_fast8_t *) malloc(sizeof(
                                               int) * gpumatmultconf[index].NBstreams);
        if(gpumatmultconf[index].refWFSinit == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }


        gpumatmultconf[index].semptr1 = (sem_t **) malloc(sizeof(
                                            sem_t *)*gpumatmultconf[index].NBstreams);
        if(gpumatmultconf[index].semptr1 == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }

        gpumatmultconf[index].semptr2 = (sem_t **) malloc(sizeof(
                                            sem_t *)*gpumatmultconf[index].NBstreams);
        if(gpumatmultconf[index].semptr2 == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }

        gpumatmultconf[index].semptr3 = (sem_t **) malloc(sizeof(
                                            sem_t *)*gpumatmultconf[index].NBstreams);
        if(gpumatmultconf[index].semptr3 == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }

        gpumatmultconf[index].semptr4 = (sem_t **) malloc(sizeof(
                                            sem_t *)*gpumatmultconf[index].NBstreams);
        if(gpumatmultconf[index].semptr4 == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }

        gpumatmultconf[index].semptr5 = (sem_t **) malloc(sizeof(
                                            sem_t *)*gpumatmultconf[index].NBstreams);
        if(gpumatmultconf[index].semptr5 == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }


        for(int device = 0; device < gpumatmultconf[index].NBstreams; device++)
        {
            gpumatmultconf[index].cMat_part[device] = (float *) malloc(sizeof(
                        float) * gpumatmultconf[index].M * gpumatmultconf[index].Nsize[device]);
            if(gpumatmultconf[index].cMat_part[device] == NULL)
            {
                printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
                exit(0);
            }

            gpumatmultconf[index].wfsVec_part[device] = (float *) malloc(sizeof(
                        float) * gpumatmultconf[index].Nsize[device]);
            if(gpumatmultconf[index].wfsVec_part[device] == NULL)
            {
                printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
                exit(0);
            }

            gpumatmultconf[index].wfsRef_part[device] = (float *) malloc(sizeof(
                        float) * gpumatmultconf[index].Nsize[device]);
            if(gpumatmultconf[index].wfsRef_part[device] == NULL)
            {
                printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
                exit(0);
            }

            gpumatmultconf[index].dmVec_part[device] = (float *) malloc(sizeof(
                        float) * gpumatmultconf[index].M);
            if(gpumatmultconf[index].dmVec_part[device] == NULL)
            {
                printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
                exit(0);
            }

            gpumatmultconf[index].dmRef_part[device] = (float *) malloc(sizeof(
                        float) * gpumatmultconf[index].M);
            if(gpumatmultconf[index].dmRef_part[device] == NULL)
            {
                printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
                exit(0);
            }


            sprintf(sname, "loop%02ld_i%02d_gpu%02d_sem1_%06d", loopnb, index,
                    GPUdevice[device], pid);
            if((gpumatmultconf[index].semptr1[device] = sem_open(sname, O_CREAT, 0644,
                    1)) == SEM_FAILED)
            {
                perror("semaphore initilization");
                exit(0);
            }
            sem_init(gpumatmultconf[index].semptr1[device], 1, 0);

            sprintf(sname, "loop%02ld_i%02d_gpu%02d_sem2_%06d", loopnb, index,
                    GPUdevice[device], pid);
            if((gpumatmultconf[index].semptr2[device] = sem_open(sname, O_CREAT, 0644,
                    1)) == SEM_FAILED)
            {
                perror("semaphore initilization");
                exit(0);
            }
            sem_init(gpumatmultconf[index].semptr2[device], 1, 0);

            sprintf(sname, "loop%02ld_i%02d_gpu%02d_sem3_%06d", loopnb, index,
                    GPUdevice[device], pid);
            if((gpumatmultconf[index].semptr3[device] = sem_open(sname, O_CREAT, 0644,
                    1)) == SEM_FAILED)
            {
                perror("semaphore initilization");
                exit(0);
            }
            sem_init(gpumatmultconf[index].semptr3[device], 1, 0);

            sprintf(sname, "loop%02ld_i%02d_gpu%02d_sem4_%06d", loopnb, index,
                    GPUdevice[device], pid);
            if((gpumatmultconf[index].semptr4[device] = sem_open(sname, O_CREAT, 0644,
                    1)) == SEM_FAILED)
            {
                perror("semaphore initilization");
                exit(0);
            }
            sem_init(gpumatmultconf[index].semptr4[device], 1, 0);

            sprintf(sname, "loop%02ld_i%02d_gpu%02d_sem5_%06d", loopnb, index,
                    GPUdevice[device], pid);
            if((gpumatmultconf[index].semptr5[device] = sem_open(sname, O_CREAT, 0644,
                    1)) == SEM_FAILED)
            {
                perror("semaphore initilization");
                exit(0);
            }
            sem_init(gpumatmultconf[index].semptr5[device], 1, 0);

        }



        // this create two threads per device
        for(int device = 0; device < gpumatmultconf[index].NBstreams; device++)
        {
            cudaSetDevice(GPUdevice[device]);
            cudaStreamCreate(&gpumatmultconf[index].stream[device]);
        }



        for(int device = 0; device < gpumatmultconf[index].NBstreams; device++)
        {
            cudaSetDevice(GPUdevice[device]);

            // ALLOCATE MEMORY ON DEVICE

            error = cudaMalloc((void **) &gpumatmultconf[index].d_cMat[device],
                               sizeof(float) * gpumatmultconf[index].M * gpumatmultconf[index].Nsize[device]);
            if(error != cudaSuccess)
            {
                printf("cudaMalloc d_cMat returned error code %d, line(%d)\n", error, __LINE__);
                exit(EXIT_FAILURE);
            }
            else
            {
                printf("ALLOCATED gpumatmultconf[%d].d_cMat[%d] size %d x %d\n", index, device,
                       (int) gpumatmultconf[index].M, (int) gpumatmultconf[index].Nsize[device]);
            }


            error = cudaMalloc((void **) &gpumatmultconf[index].d_wfsVec[device],
                               sizeof(float) * gpumatmultconf[index].Nsize[device]);
            if(error != cudaSuccess)
            {
                printf("cudaMalloc d_wfsVec returned error code %d, line(%d)\n", error,
                       __LINE__);
                exit(EXIT_FAILURE);
            }
            else
            {
                printf("ALLOCATED gpumatmultconf[%d].d_wfsVec[%d] size %d\n", index, device,
                       (int) gpumatmultconf[index].Nsize[device]);
            }

            error = cudaMalloc((void **) &gpumatmultconf[index].d_wfsRef[device],
                               sizeof(float) * gpumatmultconf[index].Nsize[device]);
            if(error != cudaSuccess)
            {
                printf("cudaMalloc d_wfsRef returned error code %d, line(%d)\n", error,
                       __LINE__);
                exit(EXIT_FAILURE);
            }
            else
            {
                printf("ALLOCATED gpumatmultconf[%d].d_wfsRef[%d] size %d\n", index, device,
                       (int) gpumatmultconf[index].Nsize[device]);
            }

            error = cudaMalloc((void **) &gpumatmultconf[index].d_dmVec[device],
                               sizeof(float) * gpumatmultconf[index].M);
            if(error != cudaSuccess)
            {
                printf("cudaMalloc d_dmVec returned error code %d, line(%d)\n", error,
                       __LINE__);
                exit(EXIT_FAILURE);
            }
            else
            {
                printf("ALLOCATED gpumatmultconf[%d].d_dmVec[%d] size %d\n", index, device,
                       (int) gpumatmultconf[index].M);
            }

            error = cudaMalloc((void **) &gpumatmultconf[index].d_dmRef[device],
                               sizeof(float) * gpumatmultconf[index].M);
            if(error != cudaSuccess)
            {
                printf("cudaMalloc d_dmVec returned error code %d, line(%d)\n", error,
                       __LINE__);
                exit(EXIT_FAILURE);
            }
            else
            {
                printf("ALLOCATED gpumatmultconf[%d].d_dmRef[%d] size %d\n", index, device,
                       (int) gpumatmultconf[index].M);
            }

            stat = cublasCreate(&gpumatmultconf[index].handle[device]);
            printf("INITIALIZED CUBLAS handle index=%d device=%d\n", index, device);
            fflush(stdout);
            if(stat != CUBLAS_STATUS_SUCCESS)
            {
                printf("CUBLAS initialization failed\n");
                return EXIT_FAILURE;
            }

        }

        for(int device = 0; device < gpumatmultconf[index].NBstreams; device++)
            for(unsigned long n = gpumatmultconf[index].Noffset[device];
                    n < gpumatmultconf[index].Noffset[device] + gpumatmultconf[index].Nsize[device];
                    n++)
            {
                gpumatmultconf[index].wfsVec_part[device][n -
                        gpumatmultconf[index].Noffset[device]] = gpumatmultconf[index].wfsVec[n];
                gpumatmultconf[index].wfsRef_part[device][n -
                        gpumatmultconf[index].Noffset[device]] = gpumatmultconf[index].wfsRef[n];
            }




        // copy memory to devices
        for(int device = 0; device < gpumatmultconf[index].NBstreams; device++)
        {
            error = cudaMemcpy(gpumatmultconf[index].d_wfsVec[device],
                               gpumatmultconf[index].wfsVec_part[device],
                               sizeof(float) * gpumatmultconf[index].Nsize[device], cudaMemcpyHostToDevice);
            if(error != cudaSuccess)
            {
                printf("cudaMemcpy d_wfsVec wfsVec returned error code %d, line(%d)\n", error,
                       __LINE__);
                exit(EXIT_FAILURE);
            }



            printf("COPY wfsRef_part to d_wfsRef\n");
            fflush(stdout);
            error = cudaMemcpy(gpumatmultconf[index].d_wfsRef[device],
                               gpumatmultconf[index].wfsRef_part[device],
                               sizeof(float) * gpumatmultconf[index].Nsize[device], cudaMemcpyHostToDevice);
            if(error != cudaSuccess)
            {
                printf("cudaMemcpy d_wfsRef wfsRef returned error code %d, line(%d)\n", error,
                       __LINE__);
                exit(EXIT_FAILURE);
            }
        }



        GPUloadCmat(index);





        printf("SETUP %d DONE, READY TO START COMPUTATIONS  ", index);
        fflush(stdout);

        gpumatmultconf[index].iret = (int *) malloc(sizeof(int) *
                                     gpumatmultconf[index].NBstreams);
        if(gpumatmultconf[index].iret == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }

        // thread data
        gpumatmultconf[index].thdata = (THDATA *) malloc(sizeof(
                                           THDATA) * gpumatmultconf[index].NBstreams);
        if(gpumatmultconf[index].thdata == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }

        gpumatmultconf[index].threadarray = (pthread_t *) malloc(sizeof(
                                                pthread_t) * gpumatmultconf[index].NBstreams);
        if(gpumatmultconf[index].threadarray == NULL)
        {
            printf("malloc allocation error - %s %d\n", __FILE__, __LINE__);
            exit(0);
        }


        for(uint32_t m = 0; m < gpumatmultconf[index].M; m++)
        {
            gpumatmultconf[index].dmVecTMP[m] = 0.0;
        }

        // cnt = 0;
        // iter = 0;
        gpumatmultconf[index].init = 1;

        printf(". . . \n");
        fflush(stdout);
    }

    for(int device = 0; device < gpumatmultconf[index].NBstreams; device++)
    {
        gpumatmultconf[index].refWFSinit[device] = initWFSref;
    }



    // printf("CONFIGURATION DONE \n");
    // fflush(stdout);


//	CUDACOMP_printGPUMATMULTCONF(index);

    return RETURN_SUCCESS;
}







//
// increments status by 4
//
int GPU_loop_MultMat_execute(
    int          index,
    int_fast8_t *status,
    int_fast8_t *GPUstatus,
    float        alpha,
    float        beta,
    int          timing,
    int          TimerOffsetIndex
)
{
    int ptn;
    //int statustot;
    int semval;
    long cnt;
    int TimerIndex;

    struct timespec tdt0[10];
    struct timespec tdt1[10];
    struct timespec tdt2[10];
    struct timespec tdt3[10];
    struct timespec tdt4[10];
    struct timespec tdt5[10];

#ifdef _PRINT_TEST
    printf("[%s] [%d]  Start (index %d)\n", __FILE__, __LINE__, index);
    fflush(stdout);
#endif


    TimerIndex = TimerOffsetIndex;

    cublasSgemv_alpha = alpha;
    cublasSgemv_beta = beta;

    // flush semaphores
    for(ptn = 0; ptn < gpumatmultconf[index].NBstreams; ptn++)
    {
        sem_getvalue(gpumatmultconf[index].semptr1[ptn], &semval);
        for(cnt = 0; cnt < semval; cnt++)
        {
            printf("WARNING %s %d  : [%ld] sem_trywait on semptr1 index %d ptn %d\n",
                   __FILE__, __LINE__, semval - cnt, index, ptn);
            fflush(stdout);
            sem_trywait(gpumatmultconf[index].semptr1[ptn]);
        }


        sem_getvalue(gpumatmultconf[index].semptr2[ptn], &semval);
        for(cnt = 0; cnt < semval; cnt++)
        {
            printf("WARNING %s %d  : [%ld] sem_trywait on semptr2 index %d ptn %d\n",
                   __FILE__, __LINE__, semval - cnt, index, ptn);
            fflush(stdout);
            sem_trywait(gpumatmultconf[index].semptr2[ptn]);
        }

        sem_getvalue(gpumatmultconf[index].semptr3[ptn], &semval);
        for(cnt = 0; cnt < semval; cnt++)
        {
            printf("WARNING %s %d  : [%ld] sem_trywait on semptr3 index %d ptn %d\n",
                   __FILE__, __LINE__, semval - cnt, index, ptn);
            fflush(stdout);
            sem_trywait(gpumatmultconf[index].semptr3[ptn]);
        }

        sem_getvalue(gpumatmultconf[index].semptr4[ptn], &semval);
        for(cnt = 0; cnt < semval; cnt++)
        {
            printf("WARNING %s %d  : [%ld] sem_trywait on semptr4 index %d ptn %d\n",
                   __FILE__, __LINE__, semval - cnt, index, ptn);
            fflush(stdout);
            sem_trywait(gpumatmultconf[index].semptr4[ptn]);
        }

        sem_getvalue(gpumatmultconf[index].semptr5[ptn], &semval);
        for(cnt = 0; cnt < semval; cnt++)
        {
            printf("WARNING %s %d  : [%ld] sem_trywait on semptr5 index %d ptn %d\n",
                   __FILE__, __LINE__, semval - cnt, index, ptn);
            fflush(stdout);
            sem_trywait(gpumatmultconf[index].semptr5[ptn]);
        }
    }


#ifdef _PRINT_TEST
    printf("[%s] [%d]  semaphores flushed\n", __FILE__, __LINE__);
    fflush(stdout);
#endif

    if(timing == 1)
    {
        *status = *status + 1;  // ->7
        clock_gettime(CLOCK_REALTIME, &tnow);
        tdiff = timespec_diff(data.image[IDtiming].md[0].atime, tnow);
        tdiffv = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;
        data.image[IDtiming].array.F[TimerIndex] = tdiffv; //25
        TimerIndex++;
    }

//    if((index==0)||(index==2)) /// main CM multiplication loop
//    {

    if(gpumatmultconf[index].CM_cnt !=
            data.image[gpumatmultconf[index].CM_ID].md[0].cnt0)
        if(data.image[gpumatmultconf[index].CM_ID].md[0].write == 0)
        {
            printf("New CM detected (cnt : %ld)\n",
                   data.image[gpumatmultconf[index].CM_ID].md[0].cnt0);
            GPUloadCmat(index);
            gpumatmultconf[index].CM_cnt =
                data.image[gpumatmultconf[index].CM_ID].md[0].cnt0;
        }
//   }



    // index is the matrix multiplication index (unique to each matrix multiplication stream operation)
    // ptn is the thread number = GPU device number


    //    if((gpumatmultconf[index].sem==0)||



    if(gpumatmultconf[index].gpuinit == 0)
    {
        printf("GPU pthread create, index = %d    %d %d\n", index,
               gpumatmultconf[index].sem, gpumatmultconf[index].gpuinit);//TEST
        fflush(stdout);

        for(ptn = 0; ptn < gpumatmultconf[index].NBstreams; ptn++)
        {
            gpumatmultconf[index].thdata[ptn].thread_no = ptn;
            gpumatmultconf[index].thdata[ptn].numl0 = ptn * ptn;
            gpumatmultconf[index].thdata[ptn].cindex = index;
            gpumatmultconf[index].thdata[ptn].status = GPUstatus;
            gpumatmultconf[index].thdata[ptn].t0 = &tdt0[ptn];
            gpumatmultconf[index].thdata[ptn].t1 = &tdt1[ptn];
            gpumatmultconf[index].thdata[ptn].t2 = &tdt2[ptn];
            gpumatmultconf[index].thdata[ptn].t3 = &tdt3[ptn];
            gpumatmultconf[index].thdata[ptn].t4 = &tdt4[ptn];
            gpumatmultconf[index].thdata[ptn].t5 = &tdt5[ptn];
            gpumatmultconf[index].iret[ptn] = pthread_create(
                                                  &gpumatmultconf[index].threadarray[ptn], NULL, GPUcomputeMVM_function,
                                                  (void *) &gpumatmultconf[index].thdata[ptn]);
            if(gpumatmultconf[index].iret[ptn])
            {
                fprintf(stderr, "Error - pthread_create() return code: %d\n",
                        gpumatmultconf[index].iret[ptn]);
                exit(EXIT_FAILURE);
            }
        }
        gpumatmultconf[index].gpuinit = 1;
    }

    if(timing == 1)
    {
        *status = *status + 1;  // -> 8
        clock_gettime(CLOCK_REALTIME, &tnow);
        tdiff = timespec_diff(data.image[IDtiming].md[0].atime, tnow);
        tdiffv = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;
        data.image[IDtiming].array.F[TimerIndex] = tdiffv; //26
        TimerIndex++;
    }


#ifdef _PRINT_TEST
    printf("[%s] [%d] - START COMPUTATION   gpumatmultconf[%d].sem = %d\n",
           __FILE__, __LINE__, index, gpumatmultconf[index].sem);
    fflush(stdout);
#endif


    if(gpumatmultconf[index].sem == 0)
    {
#ifdef _PRINT_TEST
        printf("[%s] [%d] - pthread join     %d streams\n", __FILE__, __LINE__,
               gpumatmultconf[index].NBstreams);
        fflush(stdout);
#endif

        for(ptn = 0; ptn < gpumatmultconf[index].NBstreams; ptn++)
        {
            pthread_join(gpumatmultconf[index].threadarray[ptn], NULL);
        }
    }
    else
    {
        for(ptn = 0; ptn < gpumatmultconf[index].NBstreams; ptn++)
        {
            sem_post(gpumatmultconf[index].semptr1[ptn]); // START COMPUTATION
            sem_post(gpumatmultconf[index].semptr4[ptn]);
        }

#ifdef _PRINT_TEST
        printf("[%s] [%d] - posted input semaphores  ( %d streams )\n", __FILE__,
               __LINE__, gpumatmultconf[index].NBstreams);
        fflush(stdout);
#endif


        for(ptn = 0; ptn < gpumatmultconf[index].NBstreams; ptn++)
        {
            sem_wait(gpumatmultconf[index].semptr5[ptn]);    // WAIT FOR RESULT
        }

#ifdef _PRINT_TEST
        printf("[%s] [%d] - output semaphores wait complete\n", __FILE__, __LINE__);
        fflush(stdout);
#endif


        // for safety, set semaphores to zerosem_getvalue(data.image[IDarray[i]].semptr[s], &semval);
        if(FORCESEMINIT == 1)
            for(ptn = 0; ptn < gpumatmultconf[index].NBstreams; ptn++)
            {
                sem_getvalue(gpumatmultconf[index].semptr5[ptn], &semval);
                for(cnt = 0; cnt < semval; cnt++)
                {
                    printf("WARNING %s %d  : sem_trywait on semptr5 index %d ptn %d\n", __FILE__,
                           __LINE__, index, ptn);
                    fflush(stdout);
                    sem_trywait(gpumatmultconf[index].semptr5[ptn]);
                }
            }
    }

#ifdef _PRINT_TEST
    printf("[%s] [%d] - \n", __FILE__, __LINE__);
    fflush(stdout);
#endif



    if(timing == 1)
    {
        tdiff = timespec_diff(tdt0[0], tdt1[0]);
        tdiffv = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;
        data.image[IDtiming].array.F[TimerIndex] = tdiffv; //27
        TimerIndex++;

        tdiff = timespec_diff(tdt1[0], tdt2[0]);
        tdiffv = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;
        data.image[IDtiming].array.F[TimerIndex] = tdiffv; //28
        TimerIndex++;

        tdiff = timespec_diff(tdt2[0], tdt3[0]);
        tdiffv = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;
        data.image[IDtiming].array.F[TimerIndex] = tdiffv; //29
        TimerIndex++;

        tdiff = timespec_diff(tdt3[0], tdt4[0]);
        tdiffv = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;
        data.image[IDtiming].array.F[TimerIndex] = tdiffv; //30
        TimerIndex++;

        tdiff = timespec_diff(tdt4[0], tdt5[0]);
        tdiffv = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;
        data.image[IDtiming].array.F[TimerIndex] = tdiffv; //31
        TimerIndex++;
    }




    // SUM RESULTS FROM SEPARATE GPUs
#ifdef _PRINT_TEST
    printf("[%s] [%d] - SUM RESULTS FROM SEPARATE GPUs\n", __FILE__, __LINE__);
    fflush(stdout);
#endif


    if(timing == 1)
    {
        *status = *status + 1;  // -> 9
        clock_gettime(CLOCK_REALTIME, &tnow);
        tdiff = timespec_diff(data.image[IDtiming].md[0].atime, tnow);
        tdiffv = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;
        data.image[IDtiming].array.F[TimerIndex] = tdiffv; //32
        TimerIndex++;
    }

    data.image[gpumatmultconf[index].IDout].md[0].write = 1;

    for(uint32_t m = 0; m < gpumatmultconf[index].M; m++)
    {
        gpumatmultconf[index].dmVecTMP[m] = 0.0;
    }



    for(ptn = 0; ptn < gpumatmultconf[index].NBstreams; ptn++)
    {
        for(uint32_t m = 0; m < gpumatmultconf[index].M; m++)
        {
            gpumatmultconf[index].dmVecTMP[m] += gpumatmultconf[index].dmVec_part[ptn][m];
        }
    }




    /*  if(data.image[gpumatmultconf[index].IDout].md[0].sem > 0)
       {
           sem_getvalue(data.image[gpumatmultconf[index].IDout].semptr[0], &semval);
           if(semval<SEMAPHORE_MAXVAL)
               sem_post(data.image[gpumatmultconf[index].IDout].semptr[0]);
       }


       if(data.image[gpumatmultconf[index].IDout].md[0].sem > 1)
           {
               sem_getvalue(data.image[gpumatmultconf[index].IDout].semptr[1], &semval);
               if(semval<SEMAPHORE_MAXVAL)
                   sem_post(data.image[gpumatmultconf[index].IDout].semptr[1]);
           }
    */



    if(timing == 1)
    {
        data.image[gpumatmultconf[index].IDout].md[0].cnt1 =
            data.image[IDtiming].md[0].cnt1;

        *status = *status + 1; // -> 10
        clock_gettime(CLOCK_REALTIME, &tnow);
        tdiff = timespec_diff(data.image[IDtiming].md[0].atime, tnow);
        tdiffv = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;
        data.image[IDtiming].array.F[TimerIndex] = tdiffv; //33
        TimerIndex++;
    }

    data.image[gpumatmultconf[index].IDout].md[0].cnt0++;
    COREMOD_MEMORY_image_set_sempost_byID(gpumatmultconf[index].IDout, -1);
    data.image[gpumatmultconf[index].IDout].md[0].write = 0;

#ifdef _PRINT_TEST
    printf("[%s] [%d] - DONE\n", __FILE__, __LINE__);
    fflush(stdout);
#endif



    return(0);
}










int GPU_loop_MultMat_free(int index)
{
    int device;

    cudaFree(gpumatmultconf[index].d_cMat);
    cudaFree(gpumatmultconf[index].d_dmVec);
    cudaFree(gpumatmultconf[index].d_wfsVec);
    cudaFree(gpumatmultconf[index].d_wfsRef);
    cudaFree(gpumatmultconf[index].d_dmRef);
    free(gpumatmultconf[index].stream);

    for(device = 0; device < gpumatmultconf[index].NBstreams; device++)
    {
        // free memory for stream
        cublasDestroy(gpumatmultconf[index].handle[device]);
        free(gpumatmultconf[index].cMat_part[device]);
        free(gpumatmultconf[index].wfsVec_part[device]);
        free(gpumatmultconf[index].dmVec_part[device]);
    }

    free(gpumatmultconf[index].cMat_part);
    free(gpumatmultconf[index].dmVec_part);
    free(gpumatmultconf[index].wfsVec_part);

    free(gpumatmultconf[index].Nsize);
    free(gpumatmultconf[index].Noffset);

    free(gpumatmultconf[index].iret);
    free(gpumatmultconf[index].threadarray);
    free(gpumatmultconf[index].thdata);

    free(gpumatmultconf[index].refWFSinit);

    free(gpumatmultconf[index].GPUdevice);

    return(0);
}








#ifdef HAVE_MAGMA






/******************* CPU memory */
#define TESTING_MALLOC_CPU( ptr, type, size )                              \
    if ( MAGMA_SUCCESS !=                                                  \
            magma_malloc_cpu( (void**) &ptr, (size)*sizeof(type) )) {      \
        fprintf( stderr, "!!!! magma_malloc_cpu failed for: %s\n", #ptr ); \
        magma_finalize();                                                  \
        exit(-1);                                                          \
    }

#define TESTING_FREE_CPU( ptr ) magma_free_cpu( ptr )


/******************* Pinned CPU memory */
#ifdef HAVE_CUBLAS
// In CUDA, this allocates pinned memory.
#define TESTING_MALLOC_PIN( ptr, type, size )                                 \
        if ( MAGMA_SUCCESS !=                                                     \
                magma_malloc_pinned( (void**) &ptr, (size)*sizeof(type) )) {      \
            fprintf( stderr, "!!!! magma_malloc_pinned failed for: %s\n", #ptr ); \
            magma_finalize();                                                     \
            exit(-1);                                                             \
        }

#define TESTING_FREE_PIN( ptr ) magma_free_pinned( ptr )
#else
// For OpenCL, we don't support pinned memory yet.
#define TESTING_MALLOC_PIN( ptr, type, size )                              \
        if ( MAGMA_SUCCESS !=                                                  \
                magma_malloc_cpu( (void**) &ptr, (size)*sizeof(type) )) {      \
            fprintf( stderr, "!!!! magma_malloc_cpu failed for: %s\n", #ptr ); \
            magma_finalize();                                                  \
            exit(-1);                                                          \
        }

#define TESTING_FREE_PIN( ptr ) magma_free_cpu( ptr )
#endif


/******************* GPU memory */
#ifdef HAVE_CUBLAS
// In CUDA, this has (void**) cast.
#define TESTING_MALLOC_DEV( ptr, type, size )                              \
        if ( MAGMA_SUCCESS !=                                                  \
                magma_malloc( (void**) &ptr, (size_t) sizeof(type)*size )) {          \
            fprintf( stderr, "!!!! magma_malloc failed for: %s  size = %ld  typesize = %d\n", #ptr, (long) size, (int) sizeof(type) );     \
            magma_finalize();                                                  \
            exit(-1);                                                          \
        }
#else
// For OpenCL, ptr is cl_mem* and there is no cast.
#define TESTING_MALLOC_DEV( ptr, type, size )                              \
        if ( MAGMA_SUCCESS !=                                                  \
                magma_malloc( &ptr, (size)*sizeof(type) )) {                   \
            fprintf( stderr, "!!!! magma_malloc failed for: %s\n", #ptr );     \
            magma_finalize();                                                  \
            exit(-1);                                                          \
        }
#endif

#define TESTING_FREE_DEV( ptr ) magma_free( ptr )









/* =============================================================================================== */
/* =============================================================================================== */
/*                                                                                                 */
/* 3. SINGULAR VALUE DECOMPOSITION, PSEUDO-INVERSE                                                 */
/*                                                                                                 */
/* =============================================================================================== */
/* =============================================================================================== */



/** @brief Test pseudo inverse
 *
 */

long CUDACOMP_MatMatMult_testPseudoInverse(
    const char *IDmatA_name,
    const char *IDmatAinv_name,
    const char *IDmatOut_name
)
{
    long IDmatA, IDmatAinv;
    long IDmatOut;

    long ii;
    float *magmaf_d_AinvA;
    float *magmaf_h_AinvA;

    uint32_t *arraysizetmp;
    magma_int_t M, N;


    /**
     *
     * IDmatA is an image loaded as a M x N matrix
     * IDmatAinv is an image loaded as a M x M matrix, representing the transpose of the pseudo inverse of IDmatA
     *
     * The input matrices can be 2D or a 3D images
     *
     * If 2D image :
     *   IDmatA    M = xsize
     *   IDmatA    N = ysize
     *
     * If 3D image :
     *   IDmatA M = xsize*ysize
     *   IDmatA N = ysize
     *
     *
     */

    ///
    /// MAGMA uses column-major matrices. For matrix A with dimension (M,N), element A(i,j) is A[ j*M + i]
    /// i = 0 ... M
    /// j = 0 ... N
    ///


    arraysizetmp = (uint32_t *) malloc(sizeof(uint32_t) * 3);

    IDmatA = image_ID(IDmatA_name);
    IDmatAinv = image_ID(IDmatAinv_name);

    if(data.image[IDmatA].md[0].naxis == 3)
    {
        /// each column (N=cst) of A is a z=cst slice of image Rmatrix
        M = data.image[IDmatA].md[0].size[0] * data.image[IDmatA].md[0].size[1];
        N = data.image[IDmatA].md[0].size[2];
    }
    else
    {
        /// each column (N=cst) of A is a line (y=cst) of Rmatrix (90 deg rotation)
        M = data.image[IDmatA].md[0].size[0];
        N = data.image[IDmatA].md[0].size[1];
    }


    /// Initialize MAGAM if needed
    if(INIT_MAGMA == 0)
    {
        magma_init();
        magma_print_environment();

        INIT_MAGMA = 1;
    }
    magma_queue_create(0, &magmaqueue);

    TESTING_MALLOC_CPU(magmaf_h_A, float, M * N);
    TESTING_MALLOC_DEV(magmaf_d_A, float, M * N);

    TESTING_MALLOC_CPU(magmaf_h_Ainv, float, M * N);
    TESTING_MALLOC_DEV(magmaf_d_Ainv, float, M * N);

    TESTING_MALLOC_CPU(magmaf_h_AinvA, float, N * N);
    TESTING_MALLOC_DEV(magmaf_d_AinvA, float, N * N);


    /// load matA in h_A -> d_A
    for(ii = 0; ii < M * N; ii++)
    {
        magmaf_h_A[ii] =  data.image[IDmatA].array.F[ii];
    }
    magma_ssetmatrix(M, N, magmaf_h_A, M, magmaf_d_A, M, magmaqueue);

    /// load matAinv in h_Ainv -> d_Ainv
    for(ii = 0; ii < M * N; ii++)
    {
        magmaf_h_Ainv[ii] =  data.image[IDmatAinv].array.F[ii];
    }
    magma_ssetmatrix(M, N, magmaf_h_Ainv, M, magmaf_d_Ainv, M, magmaqueue);


    magma_sgemm(MagmaTrans, MagmaNoTrans, N, N, M, 1.0, magmaf_d_Ainv, M,
                magmaf_d_A, M, 0.0,  magmaf_d_AinvA, N, magmaqueue);

    magma_sgetmatrix(N, N, magmaf_d_AinvA, N, magmaf_h_AinvA, N, magmaqueue);


    arraysizetmp[0] = N;
    arraysizetmp[1] = N;
    IDmatOut = create_image_ID(IDmatOut_name, 2, arraysizetmp, _DATATYPE_FLOAT, 0,
                               0);


    for(ii = 0; ii < N * N; ii++)
    {
        data.image[IDmatOut].array.F[ii] = magmaf_h_AinvA[ii];
    }



    TESTING_FREE_CPU(magmaf_h_AinvA);
    TESTING_FREE_DEV(magmaf_d_AinvA);

    TESTING_FREE_DEV(magmaf_d_A);
    TESTING_FREE_CPU(magmaf_h_A);

    TESTING_FREE_DEV(magmaf_d_Ainv);
    TESTING_FREE_CPU(magmaf_h_Ainv);


    free(arraysizetmp);

    magma_queue_destroy(magmaqueue);
    magma_finalize();                                //  finalize  Magma


    return IDmatOut;
}





//
// Computes control matrix
// Conventions:
//   m: number of actuators
//   n: number of sensors
int CUDACOMP_magma_compute_SVDpseudoInverse_SVD(
    const char *ID_Rmatrix_name,
    const char *ID_Cmatrix_name,
    double SVDeps,
    long MaxNBmodes,
    const char *ID_VTmatrix_name
)
{
    uint32_t *arraysizetmp;
    magma_int_t M, N, min_mn;
    long m, n, ii, jj, k;
    long ID_Rmatrix;
    long ID_Cmatrix;
    uint8_t datatype;

    magma_int_t lda, ldu, ldv;
    //float dummy[1];
    float *a, *h_R;         // a, h_R - mxn  matrices
    float *U, *VT;			// u - mxm matrix , vt - nxn  matrix  on the  host
    float *S1;              //  vectors  of  singular  values
    magma_int_t  info;
    //float  work[1];				// used in  difference  computations
    float  *h_work;           //  h_work  - workspace
    magma_int_t  lwork;                     //  workspace  size
    real_Double_t gpu_time;
    //real_Double_t cpu_time;

    FILE *fp;
    char fname[200];
    long ID_VTmatrix;
    float egvlim;
    long MaxNBmodes1, mode;


    arraysizetmp = (uint32_t *) malloc(sizeof(uint32_t) * 3);

    ID_Rmatrix = image_ID(ID_Rmatrix_name);
    datatype = data.image[ID_Rmatrix].md[0].datatype;


    if(data.image[ID_Rmatrix].md[0].naxis == 3)
    {
        n = data.image[ID_Rmatrix].md[0].size[0] * data.image[ID_Rmatrix].md[0].size[1];
        m = data.image[ID_Rmatrix].md[0].size[2];
        printf("3D image -> %ld %ld\n", n, m);
        fflush(stdout);
    }
    else
    {
        n = data.image[ID_Rmatrix].md[0].size[0];
        m = data.image[ID_Rmatrix].md[0].size[1];
        printf("2D image -> %ld %ld\n", n, m);
        fflush(stdout);
    }

    M = n;
    N = m;

    lda = M;
    ldu = M;
    ldv = N;

    min_mn = min(M, N);

    //printf("INITIALIZE MAGMA\n");
    //fflush(stdout);

    /* in this procedure, m=number of actuators/modes, n=number of WFS elements */
    //   printf("magma :    M = %ld , N = %ld\n", (long) M, (long) N);
    //fflush(stdout);



    magma_init();  // initialize Magma
    //  Allocate  host  memory
    magma_smalloc_cpu(&a, lda * N);                 // host  memory  for a
    magma_smalloc_cpu(&VT, ldv * N);                // host  memory  for vt
    magma_smalloc_cpu(&U, M * M);                   // host  memory  for u
    magma_smalloc_cpu(&S1, min_mn);                 // host  memory  for s1
    magma_smalloc_pinned(&h_R, lda * N);            // host  memory  for r
    magma_int_t  nb = magma_get_sgesvd_nb(M, N);    // opt. block  size
    lwork = (M + N) * nb + 3 * min_mn;
    magma_smalloc_pinned(&h_work, lwork);           // host  mem. for  h_work






    // write input h_R matrix
    if(datatype == _DATATYPE_FLOAT)
    {
        for(k = 0; k < m; k++)
            for(ii = 0; ii < n; ii++)
            {
                h_R[k * n + ii] =  data.image[ID_Rmatrix].array.F[k * n + ii];
            }
    }
    else
    {
        for(k = 0; k < m; k++)
            for(ii = 0; ii < n; ii++)
            {
                h_R[k * n + ii] = data.image[ID_Rmatrix].array.D[k * n + ii];
            }
    }

    //printf("M = %ld   N = %ld\n", (long) M, (long) N);
    //printf("=============== lwork = %ld\n", (long) lwork);
    gpu_time = magma_wtime();
    magma_sgesvd(MagmaSomeVec, MagmaAllVec, M, N, h_R, lda, S1, U, ldu, VT, ldv,
                 h_work, lwork, &info);
    gpu_time = magma_wtime() - gpu_time;
    if(info != 0)
    {
        printf("magma_sgesvd returned error %d: %s.\n",
               (int) info, magma_strerror(info));
    }

    //printf("sgesvd gpu time: %7.5f\n", gpu_time );


    // Write eigenvalues
    sprintf(fname, "eigenv.dat.magma");
    if((fp = fopen(fname, "w")) == NULL)
    {
        printf("ERROR: cannot create file \"%s\"\n", fname);
        exit(0);
    }
    for(k = 0; k < min_mn; k++)
    {
        fprintf(fp, "%5ld %20g %20g\n", k, S1[k], S1[k] / S1[0]);
    }
    fclose(fp);


    egvlim = SVDeps * S1[0];

    MaxNBmodes1 = MaxNBmodes;
    if(MaxNBmodes1 > M)
    {
        MaxNBmodes1 = M;
    }
    if(MaxNBmodes1 > N)
    {
        MaxNBmodes1 = N;
    }
    mode = 0;
    while((mode < MaxNBmodes1) && (S1[mode] > egvlim))
    {
        mode++;
    }
    MaxNBmodes1 = mode;

    //printf("Keeping %ld modes  (SVDeps = %g)\n", MaxNBmodes1, SVDeps);
    // Write rotation matrix
    arraysizetmp[0] = m;
    arraysizetmp[1] = m;
    if(datatype == _DATATYPE_FLOAT)
    {
        ID_VTmatrix = create_image_ID(ID_VTmatrix_name, 2, arraysizetmp,
                                      _DATATYPE_FLOAT, 0, 0);
        for(ii = 0; ii < m; ii++) // modes
            for(k = 0; k < m; k++) // modes
            {
                data.image[ID_VTmatrix].array.F[k * m + ii] = (float) VT[k * m + ii];
            }
    }
    else
    {
        ID_VTmatrix = create_image_ID(ID_VTmatrix_name, 2, arraysizetmp,
                                      _DATATYPE_DOUBLE, 0, 0);
        for(ii = 0; ii < m; ii++) // modes
            for(k = 0; k < m; k++) // modes
            {
                data.image[ID_VTmatrix].array.D[k * m + ii] = (double) VT[k * m + ii];
            }
    }


    if(data.image[ID_Rmatrix].md[0].naxis == 3)
    {
        arraysizetmp[0] = data.image[ID_Rmatrix].md[0].size[0];
        arraysizetmp[1] = data.image[ID_Rmatrix].md[0].size[1];
        arraysizetmp[2] = m;
    }
    else
    {
        arraysizetmp[0] = n;
        arraysizetmp[1] = m;
    }

    if(datatype == _DATATYPE_FLOAT)
    {
        ID_Cmatrix = create_image_ID(ID_Cmatrix_name,
                                     data.image[ID_Rmatrix].md[0].naxis, arraysizetmp, _DATATYPE_FLOAT, 0, 0);
    }
    else
    {
        ID_Cmatrix = create_image_ID(ID_Cmatrix_name,
                                     data.image[ID_Rmatrix].md[0].naxis, arraysizetmp, _DATATYPE_DOUBLE, 0, 0);
    }

    // compute pseudo-inverse
    // M+ = V Sig^-1 UT
    for(ii = 0; ii < M; ii++)
        for(jj = 0; jj < N; jj++)
            for(mode = 0; mode < MaxNBmodes1 - 1; mode++)
            {
                data.image[ID_Cmatrix].array.F[jj * M + ii] += VT[jj * N + mode] * U[mode * M +
                        ii] / S1[mode];
            }



    magma_free_cpu(a);                                        // free  host  memory
    magma_free_cpu(VT);                                       // free  host  memory
    magma_free_cpu(S1);                                       // free  host  memory
    magma_free_cpu(U);                                        // free  host  memory
    magma_free_pinned(h_work);                   // free  host  memory
    magma_free_pinned(h_R);                        // free  host  memory

    magma_finalize();                                //  finalize  Magma

    free(arraysizetmp);

    //    printf("[CM magma SVD done]\n");
    //   fflush(stdout);

    return(ID_Cmatrix);
}












/**
 *  @brief Computes matrix pseudo-inverse (AT A)^-1 AT, using eigenvector/eigenvalue decomposition of AT A
 *
 *
 * Computes pseuso inverse of a matrix.\n
 * Column-major representation used to match magma and lapack.\n
 * When viewed as an image, matrix leading dimension is size[0] = horizontal axis. When viewed in an image viewer, the first column is on the bottom side with the first element in bottom left corner, so the matrix appears rotated counter-clockwise by 90deg from its conventional representation where first column is on the left side.\n
 * Returns transpose of pseudoinverse.\n
 *
 *
 *
 * ## Matrix representation details
 *
 * Using column-major indexing\n
 * When viewed as a FITS file, the first matrix column (= vector) appears as the bottom line of the FITS image.\n
 * First matrix element is bottom left corner, second element is immediately to the right of it.
 *
 * Noting elements as a[row,column] = a[i,j], elements are accessed as in memory as:
 * 		a[ j * M + i ]
 *
 * FITS file representation (ds9 view) starts from bottom left corner.
 *
 * 		a[000,N-1] -> a[001,N-1] -> ... -> a[M-1,N-1]
 * 		............................................. ^
 * 		a[000,001] -> a[001,001] -> ... -> a[M-1,001] ^
 * 		a[000,000] -> a[001,000] -> ... -> a[M-1,000] ^     : this is the first matrix row
 *
 * Note that a tall input matrix (M>N) will appear short if viewed as an image.
 * To view the FITS file in the conventional matrix view, rotate by 90 deg clockwise.
 *
 *
 *
 * ## Application Notes
 *
 *  Use LOOPmode = 1 for computing the same size SVD, same input and output location
 *
 * ### Use case: Response matrix to compute control matrix
 *
 * When using function to invert AO response matrix with AOloopControl module, input is 2D or 3D image:
 * 		M: number of sensors    (AO control) =  size[0] (2D) = size[0]*size[1] (3D)
 * 		N: number of actuators  (AO control) =  size[1] (2D) =         size[2] (3D)
 *
 * 	We assume M>N
 *
 *
 * ### Use case: Predictive control
 *
 * When using function to compute pseudo-inverse of data matrix (predictive control), input matrix is a 2D image which is the Transpose of the data matrix.
 *		M: number of measurements samples  = size[0] (2D)
 *		N: dimension of each measurement   = size[1] (2D)
 *
 * We assume M>N
 *
 *
 *
 *
 * ## Algorithm details and main computation steps
 *
 * Notations:
 * 	AT is transpose of A
 * 	A+ is pseudo inverse of A
 *
 *  Computes pseudo-inverse : A+ = (AT A)^-1 AT
 *  Inverse of AT A is computed by SVD
 *
 * SVD:   A = U S V^T
 *   U are eigenvectors of A A^T
 *   V are eigenvectors of A^T A, computed at step 4 below
 *
 * Linear algebra reminder: equivalence between (AT A)^-1 AT and V S^-1 UT
 *
 * Definition of pseudoinverse:
 * A+ = (AT A)^-1 AT
 * singular value decomposition of A = U S VT
 * A+ = ( V S UT U S VT )^-1 V S UT
 * Since U is unitary, UT U = Id ->
 * A+ = ( V S^2 VT )^-1 V S UT
 * A+ = VT^-1 S^-2 V^-1 V S UT
 * A+ = V S^-1 UT
 *
 *  Main steps (non-QDWH):
 *
 *  STEP 1 :   Fill input data into magmaf_h_A on host
 *
 *  STEP 2 :   Copy input data to GPU                                 -> magmaf_d_A        (MxN matrix on device)
 *
 *  STEP 3 :   Compute  trans(A) x A   : magmaf_d_A x magmaf_d_A      -> magmaf_d_AtA      (NxN matrix on device)
 *
 *  STEP 4 :   Compute eigenvalues and eigenvectors of A^T A          -> magmaf_d_AtA      (NxN matrix on device)
 *     Calls magma_ssyevd_gpu :
 *     Compute the eigenvalues and optionally eigenvectors of a symmetric real matrix in single precision, GPU interface, big matrix.
 *     This function computes in single precision all eigenvalues and, optionally, eigenvectors of a real symmetric matrix A defined on the device.
 *     The  first parameter can take the values MagmaVec,'V' or MagmaNoVec,'N' and answers the question whether the eigenvectors are desired.
 *     If the eigenvectors are desired, it uses a divide and conquer algorithm.  The symmetric matrix A can be stored in lower (MagmaLower,'L')
 *     or upper  (MagmaUpper,'U') mode. If the eigenvectors are desired, then on exit A contains orthonormal eigenvectors.
 *     The eigenvalues are stored in an array w
 *
 *  STEP 5 :   Set eigenvalue limit
 *
 *  STEP 6 :   Write eigenvectors to V^T matrix
 *
 *  STEP 7 :   Write eigenvectors/eigenvalue to magma_h_VT1 if eigenvalue > limit
 *           Copy to magma_d_VT1
 *
 *  STEP 8 :   Compute M2 = VT1 VT. M2 is (AT A)^-1
 *
 *  STEP 9 :   Compute Ainv = M2 A. This is the pseudo inverse
 *
 * @note SVDeps^2 is applied as a limit to the eigenvectors of AT A, which are equal to the squares of the singular values of A, so this is equivalent to applying SVDeps as a limit on the singular values of A
 * @note When used to compute AO control matrix, N=number of actuators/modes, M=number of WFS elements
 * @note EIGENVALUES are good to about 1e-6 of peak eigenvalue in single precision, much better with double precision
 * @note 2.5x faster in single precision
 *
 * @note If provided with an additional data matrix named "", an additional Matrix Matrix product between Ainv and the provided matrix will be performed. This feature is used for predictive control and sensor fusion to create a control matrix.
 *
 * TEST MODE OUTPOUT
 *
 * non-QDWH mode:
 *
 * test_mA.fits               content of magmaf_h_A
 * test_mAtA.fits             content of transpose(A) x A = magmaf_d_AtA (output of STEP 3)
 * test_eigenv.dat            list of eigenvalues
 * test_SVDmodes.log          number of singular values kept
 * test_mM2.fits              matrix M2 (output of STEP 8)
 * test_mVT.fits              matrix transpose(V) = eigenvectors (output of step 6)
 * test_mAinv.fits            transpose of pseudoinverse
 * test_AinvA.fits            product of Ainv with A, should be close to identity matrix size NxN
 *
 *
 * QDWH mode:
 *
 * test_mA.QDWH.fits          content of magmaf_h_A
 * test_Aorig.QDWH.txt        content of magmaf_h_A prior to calling psinv function
 * test_sv.QDWH.dat           singular values after call to psinv function
 * test_SVDmodes.QDWH.log     number of singular values kept (note : independent form pseudo-inverse computation)
 * test_mAinv.QDWH.fits       transpose of pseudoinverse
 * test_AinvA.QDWH.fits       product of Ainv with A, should be close to identity matrix size NxN
 */


int CUDACOMP_magma_compute_SVDpseudoInverse(
    const char *ID_Rmatrix_name,
    const char *ID_Cmatrix_name,
    double      SVDeps,
    long        MaxNBmodes,
    const char *ID_VTmatrix_name,
    int         LOOPmode,
    int         PSINV_MODE,
    __attribute__((unused)) double      qdwh_s,
    __attribute__((unused)) float       qdwh_tol,
    int 		testmode
)
{
    long         ID_Rmatrix;
    uint8_t      datatype;
    uint32_t    *arraysizetmp;
    magma_int_t  N, M;
    long ii, jj, k;
    magma_int_t  info;


    imageID ID_PFfmdat = -1;  // optional final M-M product
    imageID ID_AtA, ID_VT, ID_Ainv;

    /// Timing tests
    // int timing = 1;                                                        /**< 1 if timing test ON, 0 otherwise */
    struct timespec t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12,
               t13;                /**< Timers                           */
    double t01d, t12d, t23d, t34d, t45d, t56d, t67d, t78d, t89d, t910d, t1011d,
           t1112d, t1213d, t013d;     /**< Times in sec                     */
    struct timespec tdiff;


    FILE *fp;
    char fname[200];

    long MaxNBmodes1, mode;


    imageID ID_Cmatrix;

    // TESTING FLAGS
    int VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse = 1;


    int MAGMAfloat =
        1;		                                               /**< 1 if single precision, 0 if double precision */



    int magmaXmode = 0; // expert mode, uses magma_ssyevdx_gpu
    // this does not speed up computation
    magma_int_t mout;




    int dAinvMODE = 0;



    //  if(timing==1)
    clock_gettime(CLOCK_REALTIME, &t0);


    /**
     *
     *
     * MATRIX REPRESENTATION CONVENTION
     *

     *
     */



    ///
    /// MAGMA uses column-major matrices. For matrix A with dimension (M,N), element A(i,j) is A[ j*M + i]
    /// i = 0 ... M : row index, coefficient of a vector
    /// j = 0 ... N : column index, vector index
    /// M is the matrix leading dimension = lda
    /// M = number of rows
    /// N = number of columns
    /// (assuming here that vector = column of the matrix)
    ///





    arraysizetmp = (uint32_t *) malloc(sizeof(uint32_t) * 3);

    ID_Rmatrix = image_ID(ID_Rmatrix_name);
    datatype = data.image[ID_Rmatrix].md[0].datatype;

    if(data.image[ID_Rmatrix].md[0].naxis == 3)
    {
        /// each column (N=cst) of A is a z=cst slice of image Rmatrix
        M = data.image[ID_Rmatrix].md[0].size[0] * data.image[ID_Rmatrix].md[0].size[1];

        N = data.image[ID_Rmatrix].md[0].size[2];

        if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
        {
            printf("3D image -> %ld %ld\n", (long) M, (long) N);
            fflush(stdout);
        }
    }
    else
    {
        /// each column (N=cst) of A is a line (y=cst) of Rmatrix (90 deg rotation)
        M = data.image[ID_Rmatrix].md[0].size[0];

        N = data.image[ID_Rmatrix].md[0].size[1];

        if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
        {
            printf("2D image -> %ld %ld\n", (long) M, (long) N);
            fflush(stdout);
        }
    }

    int m32 = r32up(M);

    //TEST
    //for(ii=0;ii<N;ii++)
    //data.image[ID_Rmatrix].array.F[ii*M+ii] += 1.0;





    if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
    {
        printf("magma :    M = %ld , N = %ld\n", (long) M, (long) N);
        fflush(stdout);
    }


    /// Initialize MAGMA if needed
    if(INIT_MAGMA == 0)
    {
        if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
        {
            printf("INITIALIZE MAGMA\n");
            fflush(stdout);
        }
        magma_init();
        magma_print_environment();

        INIT_MAGMA = 1;
    }





    // Check if using psinv library

    printf("-- PSINV_MODE = %d\n", PSINV_MODE);

    int mode_QDWHPartial;  // 1 do QDWHPartial, MAGMA otherwise
    if(PSINV_MODE == 1)
    {
        mode_QDWHPartial = 1;
#ifndef HAVE_QDWHpartial
        printf("-- QDWHpartial NOT defined  -> FORCING mode_QDWHPartial=0\n");
        mode_QDWHPartial = 0;
#endif
    }
    else
    {
        mode_QDWHPartial = 0;
    }

    printf("-- PSINV_MODE = %d  mode_QDWHPartial = %d\n", PSINV_MODE,
           mode_QDWHPartial);




    // =================================================================
    //             MEMORY ALLOCATION
    //
    // (for single precision, magma_ -> magmaf_)
    //
    // ----- QSWHpartial --------
    // magma_h_A
    // magma_d_A
    // magma_h_S
    // magma_d_U
    // magma_d_VT
    // magma_d_B
    //
    // ----- std magma ----------
    // magma_h_A
    // magma_d_A
    // magma_h_AtA
    // magma_d_AtA
    // magma_h_VT1
    // magma_d_VT1
    // magma_d_M2
    //
    // =================================================================

    if(mode_QDWHPartial)    // START mode_QDWHPartial ------------------
    {
#ifdef HAVE_QDWHpartial

        // QDWHpartial set by   mode_QDWHPartial: 1 do QDWHPartial, MAGMA otherwise
        int fact = 1; // 1 use PO-based QDWH iter, QR-based otherwise
        int psinv = 1; // 1 calculate psinv, no otherwise
        double s = 1.e-16; // Threshold for to capture a subset of the singular values
        float tol = 1.e-0; // Threshold used during rank revealing QR
        int sizeS = 0, wanted = 0, it = 0;

        int n2 = 2 * min_mn;
        int n232 = r32up(n2);
        int lddb  = n232;

        int ldd_min_mn = r32up(min_mn);
        int ldd_max_mn = r32up(max_mn);
        int ldda = ldd_max_mn;


        int n32 = r32up(N);
        int lddu  = m32;
        int lddvt = n32;

        int min_mn = min(M, N);
        int max_mn = max(M, N);

        s = qdwh_s;
        tol = qdwh_tol;


        if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
        {
            printf("ALLOCATION FOR PSINV QDWHPartial M: %d N: %d min: %d\n", (int) M,
                   (int) N, (int) min_mn);
            fflush(stdout);
        }

        if(MAGMAloop_iter == 0) /// memory is only allocated on first pass
        {


            if(MAGMAfloat == 0)
            {
                TESTING_MALLOC_CPU(magma_h_A, double, M * N);
                TESTING_MALLOC_DEV(magma_d_A, double, m32 * N);

                TESTING_MALLOC_CPU(magma_h_S, double, min_mn);

                TESTING_MALLOC_DEV(magma_d_U, double, lddu * min_mn);
                TESTING_MALLOC_DEV(magma_d_VT, double, lddvt * N);
                TESTING_MALLOC_DEV(magma_d_B, double, lddb * min_mn);

                if(testmode == 1)
                {
                    TESTING_MALLOC_DEV(magma_d_AtA, double, N * N); // used for testing
                    TESTING_MALLOC_CPU(magma_h_AtA, double, N * N);
                }
            }
            else
            {
                TESTING_MALLOC_CPU(magmaf_h_A, float, M * N);
                TESTING_MALLOC_DEV(magmaf_d_A, float, m32 * N);

                TESTING_MALLOC_CPU(magmaf_h_S, float, min_mn);

                TESTING_MALLOC_DEV(magmaf_d_U, float, lddu * min_mn);
                TESTING_MALLOC_DEV(magmaf_d_VT, float, lddvt * N);
                TESTING_MALLOC_DEV(magmaf_d_B, float, lddb * min_mn);

                if(testmode == 1)
                {
                    TESTING_MALLOC_DEV(magmaf_d_AtA, float, N * N); // used for testing
                    TESTING_MALLOC_CPU(magmaf_h_AtA, float, N * N);
                }
            }
        }
#endif
    }  // END mode_QDWHPartial -----------------------------------------
    else
    {
        if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
        {
            printf("ALLOCATION FOR PSINV MAGMA M=%ld N=%ld\n", (long) M, (long) N);
            fflush(stdout);
        }

        if(MAGMAloop_iter == 0) /// memory is only allocated on first pass
        {
            if(MAGMAfloat == 0)
            {
                TESTING_MALLOC_CPU(magma_h_A, double, M * N);
                TESTING_MALLOC_DEV(magma_d_A, double, M * N);

                TESTING_MALLOC_CPU(magma_h_AtA, double, N * N);
                TESTING_MALLOC_DEV(magma_d_AtA, double, N * N);

                TESTING_MALLOC_CPU(magma_h_VT1, double, N * N);
                TESTING_MALLOC_DEV(magma_d_VT1, double, N * N);
                TESTING_MALLOC_DEV(magma_d_M2, double, N * N);
            }
            else
            {
                TESTING_MALLOC_CPU(magmaf_h_A, float, M * N);
                printf("Allocating magmaf_d_A on device ...\n");
                fflush(stdout);
                TESTING_MALLOC_DEV(magmaf_d_A, float, M * N);
                printf(" ... done\n");
                fflush(stdout);

                TESTING_MALLOC_CPU(magmaf_h_AtA, float, N * N);
                TESTING_MALLOC_DEV(magmaf_d_AtA, float, N * N);

                TESTING_MALLOC_CPU(magmaf_h_VT1, float, N * N);
                TESTING_MALLOC_DEV(magmaf_d_VT1, float, N * N);
                TESTING_MALLOC_DEV(magmaf_d_M2, float, N * N);
            }
        }
    }




    if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
    {
        printf("MAGMA READY\n");
        fflush(stdout);
    }


    if(MAGMAloop_iter == 0)
    {
        magma_queue_create(0, &magmaqueue);
    }
    if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
    {
        printf("MAGMA: CREATE QUEUE\n");
        fflush(stdout);
    }

    // if(timing==1)
    magma_queue_sync(magmaqueue);
    clock_gettime(CLOCK_REALTIME, &t1);


    // ****************************************************
    // STEP 1 :   Fill input data into magmaf_h_A on host
    // ****************************************************
    // magma array is column-major.
    //


    if(datatype == _DATATYPE_FLOAT)
    {
        if(MAGMAfloat == 1)
        {
            if((testmode == 1)
                    || (mode_QDWHPartial ==
                        1)) // need magmaf_h_A, otherwise, straight to magmaf_d_A
            {
                if(mode_QDWHPartial == 1)
                {
                    /*for(ii=0; ii<M; ii++)
                    	for(jj=0; jj<N; jj++)
                    		magmaf_h_A[jj*m32+ii] =  data.image[ID_Rmatrix].array.F[jj*M+ii];*/
                    memcpy(magmaf_h_A, data.image[ID_Rmatrix].array.F, sizeof(float)*M * N);
                    // copy to device
                    magma_ssetmatrix(M, N, magmaf_h_A, M, magmaf_d_A, m32, magmaqueue);

                }
                else
                {
                    memcpy(magmaf_h_A, data.image[ID_Rmatrix].array.F, sizeof(float)*M * N);
                    // copy from host to device
                    magma_ssetmatrix(M, N, magmaf_h_A, M, magmaf_d_A, M, magmaqueue);
                }
            }
            else
            {
                magma_ssetmatrix(M, N, data.image[ID_Rmatrix].array.F, M, magmaf_d_A, M,
                                 magmaqueue);
            }
        }
        else
        {
            for(ii = 0; ii < M * N; ii++)
            {
                magma_h_A[ii] =  data.image[ID_Rmatrix].array.F[ii];
            }

            // copy from host to device
            magma_dsetmatrix(M, N, magma_h_A, M, magma_d_A, M, magmaqueue);
        }
    }
    else
    {
        if(MAGMAfloat == 1)
        {
            for(ii = 0; ii < M * N; ii++)
            {
                magmaf_h_A[ii] = data.image[ID_Rmatrix].array.D[ii];
            }

            // copy from host to device
            magma_ssetmatrix(M, N, magmaf_h_A, M, magmaf_d_A, M, magmaqueue);
        }
        else
        {
            if(testmode == 1) // need magma_h_A for testing
            {
                //for(ii=0; ii<M*N; ii++)
                //    magma_h_A[ii] = data.image[ID_Rmatrix].array.D[ii];
                memcpy(magma_h_A, data.image[ID_Rmatrix].array.D, sizeof(double)*M * N);
                // copy from host to device
                magma_dsetmatrix(M, N, magma_h_A, M, magma_d_A, M, magmaqueue);
            }
            else
            {
                magma_dsetmatrix(M, N, data.image[ID_Rmatrix].array.D, M, magma_d_A, M,
                                 magmaqueue);
            }
        }
    }


    if(testmode == 1)
    {
        long ID_A;

        ID_A = create_2Dimage_ID("mA", M, N);
        if(MAGMAfloat == 1)
        {
            for(ii = 0; ii < M * N; ii++)
            {
                data.image[ID_A].array.F[ii] = magmaf_h_A[ii];
            }
        }
        else
        {
            for(ii = 0; ii < M * N; ii++)
            {
                data.image[ID_A].array.F[ii] = magma_h_A[ii];
            }
        }

        if(mode_QDWHPartial == 0)
        {
            save_fits("mA", "!test_mA.fits");
        }
        else
        {
            save_fits("mA", "!test_mA.QDWH.fits");
        }
        delete_image_ID("mA");
    }









    // ****************************************************
    // STEP 2 :   Copy input data from CPU to GPU
    // ****************************************************

    if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
    {
        printf("MAGMA: OFFLOAD TO THE GPU\n");
        fflush(stdout);
    }

    // copy from host to device
    //




    // testing input to QDWH
    if(testmode == 1)
        if(mode_QDWHPartial)
        {
            sprintf(fname, "test_Aorig.QDWH.txt");
            if((fp = fopen(fname, "w")) == NULL)
            {
                printf("ERROR: cannot create file \"%s\"\n", fname);
                exit(0);
            }
            for(ii = 0; ii < M; ii++)
            {
                for(jj = 0; jj < N; jj++)
                {
                    fprintf(fp, "%e\t", *(magmaf_h_A + ii + jj * M));
                }
                fprintf(fp, "\n");
            }
            fclose(fp);
        }





    if(MAGMAloop_iter == 0)
    {
        if(MAGMAfloat == 1)
        {
            TESTING_MALLOC_CPU(magmaf_h_Ainv, float, N * M);
        }
        else
        {
            TESTING_MALLOC_CPU(magma_h_Ainv, double, N * M);
        }
    }


    if(mode_QDWHPartial)
    {
        // START mode_QDWHPartial =======================================
#ifdef HAVE_QDWHpartial
        magma_queue_sync(magmaqueue);
        clock_gettime(CLOCK_REALTIME, &t2);
        if(MAGMAfloat)
        {
            cublasHandle_t handle;
            cublasCreate(&handle);
            if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
            {
                printf("QDWHPartial: COMPUTE PSEUDO-INVERSE (float supported only!)\n");
                fflush(stdout);
            }




            /*
             * Save the matrix
            FILE *file;
            file = fopen("Aorig.txt", "w");
            for(ii=0;ii<M;ii++){
                 for(jj=0;jj<N;jj++){
                       fprintf(file, "%e\t", *(magmaf_h_A+ii+jj*M));}
                       fprintf(file, "\n");
            }
            fclose(file);
             */

            printf("\n");
            printf("CALLING QDWHpartial\n");
            printf("     fact  = %d\n", fact);
            printf("     psinv = %d\n", psinv);
            printf("     s     = %e\n", s);
            printf("     tol   = %e\n", tol);

            printf("     m32   = %d\n", m32);
            printf("     lddu  = %d\n", lddu);
            printf("     lddvt = %d\n", lddvt);
            printf("     lddb  = %d\n", lddb);

            printf("\n");

            //
            // threshold = singular value threshold relative to 1
            // tol = 1.0 to 0.1 affects QR truncation
            //
            //

            QDWHpartial(M, N,
                        fact,
                        psinv,
                        s, // Threshold to capture a subset of the singular values
                        tol, // Tolerance (governs accuracy)
                        magmaf_d_A, m32, // M // matrix
                        magmaf_h_S,          // Sigular values, size n, tau = S in QDWH
                        magmaf_d_U,  lddu, //M, //lddu, // Left singular vectors, size mx(10%n)
                        magmaf_d_VT,
                        lddvt, // N, //lddvt,// Right singular vectors, size nxn, d_VT = d_VT
                        magmaf_d_B,
                        lddb, //n2, //lddb, // Needed for the QR fact in QDWH, it is of size NxN, because the matrix will reduced
                        magmaf_h_Ainv, N,
                        &sizeS,
                        &wanted,
                        &it,
                        &flops,
                        magmaqueue, handle);

            for(int elem = 0; elem < 10; elem++)
            {
                printf("TEST magmaf_h_Ainv[%2d] = %.16f\n", elem, magmaf_h_Ainv[elem]);
            }

            printf("==========================> Projected size %d Wanted %d\n", sizeS,
                   wanted);
            magma_queue_sync(magmaqueue);
            clock_gettime(CLOCK_REALTIME, &t3);
            /*
            */
            if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
            {
                printf("Write singular values to file\n");
                fflush(stdout);
            }

            if(testmode == 1)
            {
                sprintf(fname, "test_sv.QDWH.dat");
                if((fp = fopen(fname, "w")) == NULL)
                {
                    printf("ERROR: cannot create file \"%s\"\n", fname);
                    exit(0);
                }
                fprintf(fp, "# Id\t\t SV\t\t\t ratio\t\t\t limit\t s\t sizeS\t wanted\n");
                for(k = 0; k < wanted; k++)
                {
                    fprintf(fp, "%5ld %20.8g  %20.8f  %20.8f %e  %5ld %5ld\n", k, magmaf_h_S[k],
                            magmaf_h_S[k] / magmaf_h_S[0], SVDeps, s, (long) sizeS, (long) wanted);
                }
                //fprintf(fp,"%5ld %20.8g  %e  %5ld %5ld\n", k, magmaf_h_S[k], s, sizeS, wanted);
                fclose(fp);
            }

            magma_queue_sync(magmaqueue);
            clock_gettime(CLOCK_REALTIME, &t4);

            // ****************************************************
            // Set singular value limit
            // ****************************************************
            double svlim;
            if(MAGMAfloat == 1)
            {
                svlim = SVDeps * magmaf_h_S[0];
            }
            else
            {
                svlim = SVDeps * magmaf_h_S[0];
            }

            MaxNBmodes1 = MaxNBmodes;
            if(MaxNBmodes1 > N)
            {
                MaxNBmodes1 = N;
            }
            if(MaxNBmodes1 > M)
            {
                MaxNBmodes1 = M;
            }
            mode = 0;

            if(MAGMAfloat == 1)
            {
                while((mode < MaxNBmodes1) && (magmaf_h_S[mode] > svlim))
                {
                    mode++;
                }
            }
            else
            {
                while((mode < MaxNBmodes1) && (magma_h_S[mode] > svlim))
                {
                    mode++;
                }
            }

            if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
            {
                printf("Keeping %ld modes  (SVDeps = %g -> %g, MaxNBmodes = %ld -> %ld)\n",
                       mode, SVDeps, svlim, MaxNBmodes, MaxNBmodes1);
                fflush(stdout);
            }


            if(testmode == 1)
            {
                fp = fopen("test_SVDmodes.QDWH.log", "w");
                fprintf(fp, "%6ld %6ld\n", mode, MaxNBmodes1);
                fclose(fp);
            }
            MaxNBmodes1 = mode;

            //printf("Keeping %ld modes  (SVDeps = %g)\n", MaxNBmodes1, SVDeps);
            magma_queue_sync(magmaqueue);
            clock_gettime(CLOCK_REALTIME, &t5);


            if(LOOPmode == 0)
            {
                if(MAGMAfloat == 1)
                {
                    TESTING_FREE_CPU(magmaf_h_S);

                    TESTING_FREE_DEV(magmaf_d_U);
                    TESTING_FREE_DEV(magmaf_d_VT);
                    TESTING_FREE_DEV(magmaf_d_B);
                }
                else
                {
                    TESTING_FREE_CPU(magma_h_S);

                    TESTING_FREE_DEV(magma_d_U);
                    TESTING_FREE_DEV(magma_d_VT);
                    TESTING_FREE_DEV(magma_d_B);
                }
            }

            //if(testmode==1)
            //{
            dAinvMODE = 1;
            if(MAGMAloop_iter == 0)
            {
                if(MAGMAfloat == 1)
                {

                    TESTING_MALLOC_DEV(magmaf_d_Ainv, float, N * M);
                }
                else
                {
                    TESTING_MALLOC_DEV(magma_d_Ainv, double, N * M);
                }
            }
            //}


            magma_queue_sync(magmaqueue);
            clock_gettime(CLOCK_REALTIME, &t6);
        }
#endif
    }  // END mode_QDWHPartial =========================================
    else
    {
        // START STD MAGMA ===============================================

        magma_queue_sync(magmaqueue);
        clock_gettime(CLOCK_REALTIME, &t2);


        if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
        {
            printf("MAGMA: COMPUTE trans(A) x A\n");
            fflush(stdout);
        }




        // ****************************************************
        // STEP 3 :   Compute trans(A) x A    : magmaf_d_A x magmaf_d_A      -> magmaf_d_AtA      (NxN matrix on device)
        // ****************************************************

        if(MAGMAfloat == 1)
        {
            magma_ssyrk(MagmaLower, MagmaTrans, N, M, 1.0, magmaf_d_A, M, 0.0, magmaf_d_AtA,
                        N, magmaqueue);
            magmablas_ssymmetrize(MagmaLower, N, magmaf_d_AtA, N, magmaqueue);

            // Slower alternative
            //magma_sgemm(  MagmaTrans, MagmaNoTrans, N, N, M, 1.0, magmaf_d_A, M, magmaf_d_A, M, 0.0,  magmaf_d_AtA, N, magmaqueue);
        }
        else
        {
            magma_dgemm(MagmaTrans, MagmaNoTrans, N, N, M, 1.0, magma_d_A, M, magma_d_A, M,
                        0.0,  magma_d_AtA, N, magmaqueue);
        }



        if(testmode == 1)
        {
            // copy from GPU to CPU
            if(MAGMAfloat == 1)
            {
                magma_sgetmatrix(N, N, magmaf_d_AtA, N, magmaf_h_AtA, N, magmaqueue);
            }
            else
            {
                magma_dgetmatrix(N, N, magma_d_AtA, N, magma_h_AtA, N, magmaqueue);
            }


            ID_AtA = create_2Dimage_ID("mAtA", N, N);
            if(MAGMAfloat == 1)
            {
                for(ii = 0; ii < N * N; ii++)
                {
                    data.image[ID_AtA].array.F[ii] = magmaf_h_AtA[ii];
                }
            }
            else
            {
                for(ii = 0; ii < N * N; ii++)
                {
                    data.image[ID_AtA].array.F[ii] = magma_h_AtA[ii];
                }
            }
            save_fits("mAtA", "!test_mAtA.fits");
        }


        //if(timing==1)
        magma_queue_sync(magmaqueue);
        clock_gettime(CLOCK_REALTIME, &t3);



        // ****************************************************
        // STEP 4 :   Compute eigenvalues and eigenvectors of AT A   -> magmaf_d_AtA      (NxN matrix on device)
        //
        // SVD of AT A = V S^2 VT
        // calls function magma_ssyevd_gpu
        //
        //
        // ****************************************************

        if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
        {
            printf("COMPUTE eigenvalues and eigenvectors of AT A\n");
            fflush(stdout);
        }

        if(MAGMAloop_iter == 0)
        {
            // get workspace size
            if(MAGMAfloat == 1)
            {
                float auxf_work[1];

                if(magmaXmode == 1)
                {
                    magma_ssyevdx_gpu(MagmaVec, MagmaRangeI, MagmaLower, N, NULL, N, 0.0, 1.0,
                                      N - MaxNBmodes, N, NULL, NULL, NULL, N, auxf_work, -1, magma_aux_iwork, -1,
                                      &info);
                }
                else
                {
                    magma_ssyevd_gpu(MagmaVec,              MagmaLower, N, NULL, N,
                                     NULL, NULL, N, auxf_work, -1, magma_aux_iwork, -1, &info);
                }
                // -> change to 2-stage magma SVD
                // evd -> evr
                // PALSMA

                // alt -> LQ reduction -> SVD magma_dgsvd (more stable numerically)

                magma_lwork  = (magma_int_t) MAGMA_S_REAL(auxf_work[0]);
            }
            else
            {
                double aux_work[1];

                magma_dsyevd_gpu(MagmaVec, MagmaLower, N, NULL, N, NULL, NULL, N, aux_work,  -1,
                                 magma_aux_iwork, -1, &info);
                magma_lwork  = (magma_int_t) MAGMA_S_REAL(aux_work[0]);
            }

            magma_liwork = magma_aux_iwork[0];
        }




        if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
        {
            printf("MAGMA: allocate & compute\n");
            fflush(stdout);
        }


        if(MAGMAloop_iter == 0)
        {
            if(MAGMAfloat == 1)
            {
                TESTING_MALLOC_CPU(magma_iwork,  magma_int_t, magma_liwork);
                TESTING_MALLOC_PIN(magmaf_h_work, float, magma_lwork);
                TESTING_MALLOC_CPU(magmaf_w1,     float,             N);
                TESTING_MALLOC_PIN(magmaf_h_R,    float, N * N);
            }
            else
            {
                TESTING_MALLOC_CPU(magma_iwork,  magma_int_t, magma_liwork);
                TESTING_MALLOC_PIN(magma_h_work, double, magma_lwork);
                TESTING_MALLOC_CPU(magma_w1,     double,             N);
                TESTING_MALLOC_PIN(magma_h_R,    double, N * N);
            }
        }


        //if(timing==1)
        magma_queue_sync(magmaqueue);
        clock_gettime(CLOCK_REALTIME, &t4);



        if(MAGMAfloat == 1)
        {

            //	printf("============== %s %d\n", __FILE__, __LINE__);
            //	fflush(stdout);

            if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
            {
                printf(" -> FLOAT [%d] magma_ssyevd_gpu -> ", magmaXmode);
                fflush(stdout);
            }

            // SSYEVD computes all eigenvalues and, optionally, eigenvectors of a real symmetric matrix A
            if(magmaXmode == 1)
            {
                magma_ssyevdx_gpu(MagmaVec, MagmaRangeI, MagmaLower, N, magmaf_d_AtA, N, 0.0,
                                  1.0, N - MaxNBmodes, N, &mout, magmaf_w1, magmaf_h_R, N, magmaf_h_work,
                                  magma_lwork, magma_iwork, magma_liwork, &info);
            }
            else
            {
                magma_ssyevd_gpu(MagmaVec,               MagmaLower, N, magmaf_d_AtA, N,
                                 magmaf_w1, magmaf_h_R, N, magmaf_h_work, magma_lwork, magma_iwork, magma_liwork,
                                 &info);
            }

            //			printf("============== %s %d\n", __FILE__, __LINE__);
            //	fflush(stdout);

            if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
            {
                printf(" DONE\n");
                fflush(stdout);
            }
        }
        else
        {
            if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
            {
                printf(" -> DOUBLE [%d] magma_dsyevd_gpu -> ", magmaXmode);
                fflush(stdout);
            }
            // CODE CAN HANG HERE - THIS HAPPENS ONCE OUT OF multiple 1000s EXECUTIONS WHEN RUNNING IN A LOOP.. SEEMS TO BE A MAGMA ISSUE

            // SSYEVD computes all eigenvalues and, optionally, eigenvectors of a real symmetric matrix A
            magma_dsyevd_gpu(MagmaVec, MagmaLower, N, magma_d_AtA, N, magma_w1, magma_h_R,
                             N, magma_h_work, magma_lwork, magma_iwork, magma_liwork, &info);

            if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
            {
                printf(" DONE\n");
                fflush(stdout);
            }
        }

        if(LOOPmode == 0)
        {
            TESTING_FREE_CPU(magma_iwork);

            if(MAGMAfloat == 1)
            {
                TESTING_FREE_PIN(magmaf_h_R);
            }
            else
            {
                TESTING_FREE_PIN(magma_h_R);
            }

            if(MAGMAfloat == 1)
            {
                TESTING_FREE_PIN(magma_h_work);
            }
            else
            {
                TESTING_FREE_PIN(magmaf_h_work);
            }
        }




        //if(timing==1)
        magma_queue_sync(magmaqueue);
        clock_gettime(CLOCK_REALTIME, &t5);


        if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
        {
            printf("Write eigenvalues to file\n");
            fflush(stdout);
        }


        if(testmode == 1)
        {
            sprintf(fname, "test_eigenv.dat");
            if((fp = fopen(fname, "w")) == NULL)
            {
                printf("ERROR: cannot create file \"%s\"\n", fname);
                exit(0);
            }
            if(MAGMAfloat == 1)
            {
                for(k = 0; k < N; k++)
                {
                    fprintf(fp, "%5ld %20.8g  %20.8f  %20.8f\n", k, magmaf_w1[N - k - 1],
                            magmaf_w1[N - k - 1] / magmaf_w1[N - 1], SVDeps * SVDeps);
                }
            }
            else
            {
                for(k = 0; k < N; k++)
                {
                    fprintf(fp, "%5ld %20.8g  %20.8f  %20.8f\n", k, magma_w1[N - k - 1],
                            magma_w1[N - k - 1] / magma_w1[N - 1], SVDeps * SVDeps);
                }
            }
            fclose(fp);
        }


        /// w1 values are the EIGENVALUES of AT A
        /// Note: w1 values are the SQUARE of the singular values of A



        // ****************************************************
        // STEP 5 :   Set eigenvalue limit
        // ****************************************************
        double egvlim;
        if(MAGMAfloat == 1)
        {
            egvlim = SVDeps * SVDeps * magmaf_w1[N - 1];
        }
        else
        {
            egvlim = SVDeps * SVDeps * magma_w1[N - 1];
        }

        MaxNBmodes1 = MaxNBmodes;
        if(MaxNBmodes1 > N)
        {
            MaxNBmodes1 = N;
        }
        if(MaxNBmodes1 > M)
        {
            MaxNBmodes1 = M;
        }
        mode = 0;

        if(MAGMAfloat == 1)
        {
            while((mode < MaxNBmodes1) && (magmaf_w1[N - mode - 1] > egvlim))
            {
                mode++;
            }
        }
        else
        {
            while((mode < MaxNBmodes1) && (magma_w1[N - mode - 1] > egvlim))
            {
                mode++;
            }
        }

        if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
        {
            printf("Keeping %ld modes  (SVDeps = %g -> %g, MaxNBmodes = %ld -> %ld)\n",
                   mode, SVDeps, egvlim, MaxNBmodes, MaxNBmodes1);
            fflush(stdout);
        }

        if(testmode == 1)
        {
            fp = fopen("test_SVDmodes.log", "w");
            fprintf(fp, "%6ld %6ld\n", mode, MaxNBmodes1);
            fclose(fp);
        }
        MaxNBmodes1 = mode;
        //printf("Keeping %ld modes  (SVDeps = %g)\n", MaxNBmodes1, SVDeps);



        // ****************************************************
        // STEP 6 :   Write eigenvectors to VT matrix
        // ****************************************************

        // eigenvectors are in magma_d_AtA (device), copy them to magma_h_AtA (host)

        if(MAGMAfloat == 1)
        {
            magma_sgetmatrix(N, N, magmaf_d_AtA, N, magmaf_h_AtA, N, magmaqueue);
        }
        else
        {
            magma_dgetmatrix(N, N, magma_d_AtA, N, magma_h_AtA, N, magmaqueue);
        }


        // copy eigenvectors from magma_h_AtA to VT
        ID_VT = create_2Dimage_ID(ID_VTmatrix_name, N, N);

        if(MAGMAfloat == 1)
        {
            for(ii = 0; ii < N; ii++)
                for(jj = 0; jj < N; jj++)
                {
                    data.image[ID_VT].array.F[jj * N + ii] = magmaf_h_AtA[(N - ii - 1) * N + jj];
                }
        }
        else
        {
            for(ii = 0; ii < N; ii++)
                for(jj = 0; jj < N; jj++)
                {
                    data.image[ID_VT].array.F[jj * N + ii] = magma_h_AtA[(N - ii - 1) * N + jj];
                }
        }

        if(testmode == 1)
        {
            save_fits("mVT", "!test_mVT.fits");
        }

        // ****************************************************
        // STEP 7 :   Write eigenvectors/eigenvalue to magma_h_VT1 if eigenvalue > limit
        //          Copy to magma_d_VT1
        // ****************************************************

        if(MAGMAfloat == 1)
        {
            for(ii = 0; ii < N; ii++)
                for(jj = 0; jj < N; jj++)
                {
                    if(N - jj - 1 < MaxNBmodes1)
                    {
                        magmaf_h_VT1[ii * N + jj] = magmaf_h_AtA[jj * N + ii] / magmaf_w1[jj];
                    }
                    else
                    {
                        magmaf_h_VT1[ii * N + jj] = 0.0;
                    }
                }
            magma_ssetmatrix(N, N, magmaf_h_VT1, N, magmaf_d_VT1, N, magmaqueue);
        }
        else
        {
            for(ii = 0; ii < N; ii++)
                for(jj = 0; jj < N; jj++)
                {
                    if(N - jj - 1 < MaxNBmodes1)
                    {
                        magma_h_VT1[ii * N + jj] = magma_h_AtA[jj * N + ii] / magma_w1[jj];
                    }
                    else
                    {
                        magma_h_VT1[ii * N + jj] = 0.0;
                    }
                }
            magma_dsetmatrix(N, N, magma_h_VT1, N, magma_d_VT1, N, magmaqueue);
        }


        if(LOOPmode == 0)
        {
            if(MAGMAfloat == 1)
            {
                TESTING_FREE_CPU(magmaf_h_VT1);
                TESTING_FREE_CPU(magmaf_w1);
            }
            else
            {
                TESTING_FREE_CPU(magma_h_VT1);
                TESTING_FREE_CPU(magma_w1);
            }
        }


        //if(timing==1)
        magma_queue_sync(magmaqueue);
        clock_gettime(CLOCK_REALTIME, &t6);


        // ****************************************************
        // STEP 8 :   Compute M2 = VT1 VT = (AT A)^-1
        // ****************************************************


        if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
        {
            printf("compute M2 = VT1 VT\n");
            fflush(stdout);
        }

        if(MAGMAfloat == 1)
        {
            if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
            {
                printf(" -> magma_sgemm ");
                fflush(stdout);
            }

            magma_sgemm(MagmaTrans, MagmaTrans, N, N, N, 1.0, magmaf_d_VT1, N, magmaf_d_AtA,
                        N, 0.0,  magmaf_d_M2, N, magmaqueue);

            if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
            {
                printf("-> DONE\n");
                fflush(stdout);
            }
        }
        else
        {
            if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
            {
                printf(" -> magma_dgemm ");
                fflush(stdout);
            }

            magma_dgemm(MagmaTrans, MagmaTrans, N, N, N, 1.0, magma_d_VT1, N, magma_d_AtA,
                        N, 0.0,  magma_d_M2, N, magmaqueue);

            if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
            {
                printf("-> DONE\n");
                fflush(stdout);
            }
        }

        if(testmode == 1)
        {
            long ID_M2;

            ID_M2 = create_2Dimage_ID("mM2", N, N);
            if(MAGMAfloat == 1)
            {
                TESTING_MALLOC_CPU(magmaf_h_M2, float, N * N);
                magma_sgetmatrix(N, N, magmaf_d_M2, N, magmaf_h_M2, N, magmaqueue);

                for(ii = 0; ii < N; ii++)
                    for(jj = 0; jj < N; jj++)
                    {
                        data.image[ID_M2].array.F[jj * N + ii] = magmaf_h_M2[jj * N + ii];
                    }
            }
            else
            {
                TESTING_MALLOC_CPU(magma_h_M2, double, N * N);
                magma_dgetmatrix(N, N, magma_d_M2, N, magma_h_M2, N, magmaqueue);

                for(ii = 0; ii < N; ii++)
                    for(jj = 0; jj < N; jj++)
                    {
                        data.image[ID_M2].array.F[jj * N + ii] = magma_h_M2[jj * N + ii];
                    }
            }

            save_fits("mM2", "!test_mM2.fits");


            //	magma_dsetmatrix( N, N, h_M2, N, d_M2, N, magmaqueue);
            if(MAGMAfloat == 1)
            {
                TESTING_FREE_CPU(magmaf_h_M2);
            }
            else
            {
                TESTING_FREE_CPU(magma_h_M2);
            }
        }

        if(LOOPmode == 0)
        {
            if(MAGMAfloat == 1)
            {
                TESTING_FREE_DEV(magmaf_d_VT1);
            }
            else
            {
                TESTING_FREE_DEV(magma_d_VT1);
            }
        }

        //if(timing==1)
        magma_queue_sync(magmaqueue);
        clock_gettime(CLOCK_REALTIME, &t7);

        // ****************************************************
        // STEP 9 :   Compute Ainv = M2 A = (AT A)^-1 A
        // ****************************************************


        // compute Ainv = M2 A
        if(MAGMAloop_iter == 0)
        {
            dAinvMODE = 1;
            if(MAGMAfloat == 1)
            {
                TESTING_MALLOC_DEV(magmaf_d_Ainv, float, N * M);
            }
            else
            {
                TESTING_MALLOC_DEV(magma_d_Ainv, double, N * M);
            }
        }

        if(MAGMAfloat == 1)
        {
            if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
            {
                printf(" -> magma_sgemm ");
                fflush(stdout);
            }

            magma_sgemm(MagmaNoTrans, MagmaNoTrans, M, N, N, 1.0, magmaf_d_A, M,
                        magmaf_d_M2, N, 0.0, magmaf_d_Ainv, M, magmaqueue);

            if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
            {
                printf("-> DONE\n");
                fflush(stdout);
            }
        }
        else
        {
            if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
            {
                printf(" -> magma_dgemm ");
                fflush(stdout);
            }

            magma_dgemm(MagmaNoTrans, MagmaNoTrans, M, N, N, 1.0, magma_d_A, M, magma_d_M2,
                        N, 0.0, magma_d_Ainv, M, magmaqueue);

            if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
            {
                printf("-> DONE\n");
                fflush(stdout);
            }
        }




        if(LOOPmode == 0)
        {
            if(MAGMAfloat == 1)
            {
                TESTING_FREE_DEV(magmaf_d_M2);
            }
            else
            {
                TESTING_FREE_DEV(magma_d_M2);
            }
        }


        //if(timing==1)
        magma_queue_sync(magmaqueue);
        clock_gettime(CLOCK_REALTIME, &t8);


        if(MAGMAfloat == 1)
        {
            magma_sgetmatrix(M, N, magmaf_d_Ainv, M, magmaf_h_Ainv, M, magmaqueue);
        }
        else
        {
            magma_dgetmatrix(M, N, magma_d_Ainv, M, magma_h_Ainv, M, magmaqueue);
        }


        for(int elem = 0; elem < 10; elem++)
        {
            printf("TEST magmaf_h_Ainv[%2d] = %.16f\n", elem, magmaf_h_Ainv[elem]);
        }


    } // END STD MAGMA =================================================
    // End of QDWHPartial / MAGMA conditional

    //
    // At this point, pseudo-inverse is in magma_h_Ainv or magmaf_h_Ainv
    //



    if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
    {
        printf("END OF PSINV\n");
        fflush(stdout);
    }

    if(testmode == 1)
    {
        ID_Ainv = create_2Dimage_ID("mAinv", M, N);
        if(MAGMAfloat == 1)
        {
            if(mode_QDWHPartial == 1)
            {
                for(ii = 0; ii < M; ii++)
                    for(jj = 0; jj < N; jj++)
                    {
                        data.image[ID_Ainv].array.F[jj * M + ii] = magmaf_h_Ainv[ii * N + jj];
                    }
            }
            else
            {
                for(ii = 0; ii < M; ii++)
                    for(jj = 0; jj < N; jj++)
                    {
                        data.image[ID_Ainv].array.F[jj * M + ii] = magmaf_h_Ainv[jj * M + ii];
                    }
            }
        }
        else
        {
            for(ii = 0; ii < M; ii++)
                for(jj = 0; jj < N; jj++)
                {
                    data.image[ID_Ainv].array.F[jj * M + ii] = magma_h_Ainv[jj * M + ii];
                }
        }

        if(mode_QDWHPartial == 0)
        {
            save_fits("mAinv", "!test_mAinv.fits");
        }
        else
        {
            save_fits("mAinv", "!test_mAinv.QDWH.fits");
        }
    }

    //if(timing==1)
    magma_queue_sync(magmaqueue);
    clock_gettime(CLOCK_REALTIME, &t9);


    if(MAGMAloop_iter == 0)
    {
        if(data.image[ID_Rmatrix].md[0].naxis == 3)
        {
            arraysizetmp[0] = data.image[ID_Rmatrix].md[0].size[0];
            arraysizetmp[1] = data.image[ID_Rmatrix].md[0].size[1];
            arraysizetmp[2] = N;
        }
        else
        {
            arraysizetmp[0] = M;
            arraysizetmp[1] = N;
        }

        if(datatype == _DATATYPE_FLOAT)
        {
            ID_Cmatrix = create_image_ID(ID_Cmatrix_name,
                                         data.image[ID_Rmatrix].md[0].naxis, arraysizetmp, _DATATYPE_FLOAT, 0, 0);
        }
        else
        {
            ID_Cmatrix = create_image_ID(ID_Cmatrix_name,
                                         data.image[ID_Rmatrix].md[0].naxis, arraysizetmp, _DATATYPE_DOUBLE, 0, 0);
        }
    }
    else
    {
        ID_Cmatrix = image_ID(ID_Cmatrix_name);
    }

    magma_queue_sync(magmaqueue);
    clock_gettime(CLOCK_REALTIME, &t10);


    if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
    {
        printf("write result\n");
        fflush(stdout);
    }


    if(datatype == _DATATYPE_FLOAT)
    {
        if(MAGMAfloat == 1)
        {
            if(mode_QDWHPartial == 1)
            {
                for(ii = 0; ii < M; ii++)
                    for(jj = 0; jj < N; jj++)
                    {
                        data.image[ID_Cmatrix].array.F[jj * M + ii] = magmaf_h_Ainv[ii * N + jj];
                    }

            }
            else
            {
                memcpy(data.image[ID_Cmatrix].array.F, magmaf_h_Ainv, sizeof(float)*M * N);
            }
        }
        else
        {
            for(ii = 0; ii < M * N; ii++)
            {
                data.image[ID_Cmatrix].array.F[ii] = (float) magma_h_Ainv[ii];
            }
        }
    }
    else
    {
        // sensors : M
        // actuator modes: N
        if(MAGMAfloat == 1)
        {
            for(ii = 0; ii < M * N; ii++)
            {
                data.image[ID_Cmatrix].array.D[ii] = magmaf_h_Ainv[ii];
            }
        }
        else
        {
            memcpy(data.image[ID_Cmatrix].array.D, magma_h_Ainv, sizeof(double)*M * N);
        }
    }
    /*
        }
    */


    //if(timing==1)
    magma_queue_sync(magmaqueue);
    clock_gettime(CLOCK_REALTIME, &t11);


    if(mode_QDWHPartial == 1)
    {
        // copy from CPU to GPU
        if(MAGMAfloat == 1)
        {
            magma_ssetmatrix(N, M, magmaf_h_Ainv, N, magmaf_d_Ainv, N, magmaqueue);
        }
        else
        {
            magma_dsetmatrix(N, M, magma_h_Ainv, N, magma_d_Ainv, N, magmaqueue);
        }
    }


    if(testmode == 1) // compute product of Ainv with A
    {
        if(mode_QDWHPartial == 1)
        {
            magma_ssetmatrix(M, N, magmaf_h_A, M, magmaf_d_A, m32, magmaqueue);
            magma_sgemm(MagmaNoTrans, MagmaNoTrans, N, N, M, 1.0, magmaf_d_Ainv, N,
                        magmaf_d_A, m32, 0.0,  magmaf_d_AtA, N, magmaqueue);
        }
        else
        {
            magma_sgemm(MagmaTrans, MagmaNoTrans, N, N, M, 1.0, magmaf_d_A, M,
                        magmaf_d_Ainv, M, 0.0,  magmaf_d_AtA, N, magmaqueue);
        }

        long ID_AinvA;

        ID_AinvA = create_2Dimage_ID("AinvA", N, N);

        // copy from GPU to CPU
        if(MAGMAfloat == 1)
        {
            magma_sgetmatrix(N, N, magmaf_d_AtA, N, magmaf_h_AtA, N, magmaqueue);
        }
        else
        {
            magma_dgetmatrix(N, N, magma_d_AtA, N, magma_h_AtA, N, magmaqueue);
        }


        if(MAGMAfloat == 1)
        {
            memcpy(data.image[ID_AinvA].array.F, magmaf_h_AtA, sizeof(float)*N * N);
        }


        if(mode_QDWHPartial == 0)
        {
            save_fits("AinvA", "!test_AinvA.fits");
        }
        else
        {
            save_fits("AinvA", "!test_AinvA.QDWH.fits");
        }
    }




    magma_queue_sync(magmaqueue);
    clock_gettime(CLOCK_REALTIME, &t12);



    ID_PFfmdat = image_ID("PFfmdat");
    if(ID_PFfmdat != -1)
    {
        printf("=============================================\n");
        printf("=========// OUTPUT M-M MULTIPLY //===========\n");
        printf("=============================================\n");

        printf("Transp(Ainv)     N x M   = %d x %d\n", N, M);
        printf("PFfmdat  M x K           = %d x %d\n",
               data.image[ID_PFfmdat].md[0].size[0], data.image[ID_PFfmdat].md[0].size[1]);
        long K = data.image[ID_PFfmdat].md[0].size[1];
        printf("K = %ld\n", K);


        float *magmaf_d_PFfmdat;
        float *magmaf_d_PF;
        float *magmaf_h_PF;


        TESTING_MALLOC_DEV(magmaf_d_PFfmdat, float, M * K);
        TESTING_MALLOC_DEV(magmaf_d_PF, float, N * K);
        TESTING_MALLOC_CPU(magmaf_h_PF, float, N * K);

        magma_ssetmatrix(M, K, data.image[ID_PFfmdat].array.F, M, magmaf_d_PFfmdat, M,
                         magmaqueue);
        if(mode_QDWHPartial == 1)
        {
            magma_sgemm(MagmaNoTrans, MagmaNoTrans, N, K, M, 1.0, magmaf_d_Ainv, N,
                        magmaf_d_PFfmdat, M, 0.0,  magmaf_d_PF, N, magmaqueue);
        }
        else
        {
            magma_sgemm(MagmaTrans, MagmaNoTrans, N, K, M, 1.0, magmaf_d_Ainv, M,
                        magmaf_d_PFfmdat, M, 0.0,  magmaf_d_PF, N, magmaqueue);
        }

        magma_sgetmatrix(N, K, magmaf_d_PF, N, magmaf_h_PF, N, magmaqueue);

        long ID_PF = create_2Dimage_ID("psinvPFmat", N, K);
        list_image_ID();
        memcpy(data.image[ID_PF].array.F, magmaf_h_PF, sizeof(float)*N * K);
        save_fits("psinvPFmat", "!psinvPFmat.fits");

        TESTING_FREE_DEV(magmaf_d_PFfmdat);
        TESTING_FREE_DEV(magmaf_d_PF);
        TESTING_FREE_CPU(magmaf_h_PF);
    }

    magma_queue_sync(magmaqueue);
    clock_gettime(CLOCK_REALTIME, &t13);


    if(LOOPmode ==
            0) /// if pseudo-inverse is only computed once, these arrays can be freed
    {
        if(MAGMAfloat == 1)
        {
            TESTING_FREE_CPU(magmaf_h_A);
        }
        else
        {
            TESTING_FREE_CPU(magma_h_A);
        }
    }

    if(LOOPmode == 0)
    {
        if(MAGMAfloat == 1)
        {
            TESTING_FREE_DEV(magmaf_d_A);

            if(dAinvMODE == 1)
            {
                TESTING_FREE_DEV(magmaf_d_Ainv);
            }

            TESTING_FREE_CPU(magmaf_h_Ainv);
            TESTING_FREE_DEV(magmaf_d_AtA);
            TESTING_FREE_CPU(magmaf_h_AtA);
        }
        else
        {
            TESTING_FREE_DEV(magma_d_A);

            if(dAinvMODE == 1)
            {
                TESTING_FREE_DEV(magma_d_Ainv);
            }

            TESTING_FREE_CPU(magma_h_Ainv);
            TESTING_FREE_DEV(magma_d_AtA);
            TESTING_FREE_CPU(magma_h_AtA);
        }
    }




    if(LOOPmode == 0)
    {
        magma_queue_destroy(magmaqueue);
        magma_finalize();                                //  finalize  Magma
    }

    free(arraysizetmp);


    //if(timing==1)
    //{
    tdiff = timespec_diff(t0, t1);
    t01d = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;

    tdiff = timespec_diff(t1, t2);
    t12d = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;

    tdiff = timespec_diff(t2, t3);
    t23d = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;

    tdiff = timespec_diff(t3, t4);
    t34d = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;

    tdiff = timespec_diff(t4, t5);
    t45d = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;

    tdiff = timespec_diff(t5, t6);
    t56d = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;

    if(mode_QDWHPartial == 0)
    {
        tdiff = timespec_diff(t6, t7);
        t67d = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;

        tdiff = timespec_diff(t7, t8);
        t78d = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;
    }
    tdiff = timespec_diff(t8, t9);
    t89d = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;

    tdiff = timespec_diff(t9, t10);
    t910d = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;

    tdiff = timespec_diff(t10, t11);
    t1011d = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;

    tdiff = timespec_diff(t11, t12);
    t1112d = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;

    tdiff = timespec_diff(t12, t13);
    t1213d = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;

    tdiff = timespec_diff(t0, t13);
    t013d = 1.0 * tdiff.tv_sec + 1.0e-9 * tdiff.tv_nsec;

    if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
    {
        printf("%6ld  Timing info: \n", MAGMAloop_iter);
        printf("  0-1	[setup]                           %12.3f ms\n", t01d * 1000.0);
        printf("  1-2	[copy input to GPU]               %12.3f ms\n", t12d * 1000.0);
        if(mode_QDWHPartial == 1)
        {
            printf("  2-3	[compute QDWH SVD]                %12.3f ms\n", t23d * 1000.0);
            printf("  4-5	[Select eigenvalues]              %12.3f ms\n", t45d * 1000.0);
        }
        else
        {
            printf("  2-3	[compute trans(A) x A]            %12.3f ms\n", t23d * 1000.0);
            printf("  3-4	[setup]                           %12.3f ms\n", t34d * 1000.0);
            printf("  4-5	[Compute eigenvalues]             %12.3f ms\n", t45d * 1000.0);
            printf("  5-6	[Select eigenvalues]              %12.3f ms\n", t56d * 1000.0);
            printf("  6-7	[Compute M2]                      %12.3f ms\n", t67d * 1000.0);
            printf("  7-8	[Compute Ainv]                    %12.3f ms\n", t78d * 1000.0);
            printf("  8-9	[Get Ainv from GPU]               %12.3f ms\n", t89d * 1000.0);
        }
        printf("  9-10	[output setup]                    %12.3f ms\n", t910d * 1000.0);
        printf("  10-11	[Write output array]              %12.3f ms\n",
               t1011d * 1000.0);
        printf("  11-12	[Test output]                     %12.3f ms\n",
               t1112d * 1000.0);
        printf("  12-13	[Optional gemm]                   %12.3f ms\n",
               t1213d * 1000.0);
        printf("\n");
        printf(" TOTAL 0-13     %12.3f ms\n", t013d * 1000.0);
        fflush(stdout);
    }
    //}



    if(VERBOSE_CUDACOMP_magma_compute_SVDpseudoInverse == 1)
    {
        printf("\n\n");
        fflush(stdout);
    }

    if(LOOPmode == 1)
    {
        MAGMAloop_iter++;
    }


    return(ID_Cmatrix);
}




#endif











//
// Computes control matrix
// Conventions:
//   n: number of actuators (= NB_MODES)
//   m: number of sensors  (= # of pixels)
// assumes m = n

errno_t GPU_SVD_computeControlMatrix(
    int device,
    const char *ID_Rmatrix_name,
    const char *ID_Cmatrix_name,
    double      SVDeps,
    const char *ID_VTmatrix_name
)
{
    cusolverDnHandle_t  cudenseH = NULL;
    cublasHandle_t      cublasH = NULL;
    cublasStatus_t      cublas_status; // = CUBLAS_STATUS_SUCCESS;
    cusolverStatus_t    cusolver_status; // = CUSOLVER_STATUS_SUCCESS;
    struct cudaDeviceProp deviceProp;

    imageID ID_Rmatrix, ID_Cmatrix, ID_VTmatrix;
    uint8_t datatype;
    uint32_t *arraysizetmp;
    int lda, ldu, ldvt;


    float *d_A  = NULL; // linear memory of GPU
    float *h_A  = NULL;
    float *d_S  = NULL; // linear memory of GPU
    float *d_U  = NULL; // linear memory of GPU
    float *h_U1 = NULL;
    float *d_VT = NULL; // linear memory of GPU
    float *d_M  = NULL; // linear memory of GPU
    float *d_U1 = NULL; // linear memory of GPU
    float *d_Work = NULL; // linear memory of GPU
    cudaError_t cudaStat = cudaSuccess;
    int *devInfo = NULL; // info in gpu (device copy)
    int Lwork;
    float *rwork;

    float *Sarray;
    //float *Aarray;
    long i;
    FILE *fp;
    char fname[200];

    int info_gpu;

    double time1sec, time2sec;
    struct timespec tnow;


    float val;
    float alpha = 1.0;
    float beta = 0.0;
    imageID ID;

    float *h_M;
    long cnt0;


    cudaGetDeviceCount(&deviceCount);
    printf("%d devices found\n", deviceCount);
    fflush(stdout);
    printf("\n");
    for(int k = 0; k < deviceCount; ++k)
    {
        cudaGetDeviceProperties(&deviceProp, k);
        printf("Device %d [ %20s ]  has compute capability %d.%d.\n",
               k, deviceProp.name, deviceProp.major, deviceProp.minor);
        printf("  Total amount of global memory:                 %.0f MBytes (%llu bytes)\n",
               (float)deviceProp.totalGlobalMem / 1048576.0f,
               (unsigned long long) deviceProp.totalGlobalMem);
        printf("  (%2d) Multiprocessors\n", deviceProp.multiProcessorCount);
        printf("  GPU Clock rate:                                %.0f MHz (%0.2f GHz)\n",
               deviceProp.clockRate * 1e-3f, deviceProp.clockRate * 1e-6f);
        printf("\n");
    }


    if(device < deviceCount)
    {
        cudaSetDevice(device);
    }
    else
    {
        printf("Invalid Device : %d / %d\n", device, deviceCount);
        exit(0);
    }

    cudaDeviceReset();

    printf("step 1a: create cudense handle ...");
    fflush(stdout);
    cusolver_status = cusolverDnCreate(&cudenseH);
    if(cusolver_status != CUSOLVER_STATUS_SUCCESS)
    {
        printf("CUSOLVER initialization failed\n");
        return EXIT_FAILURE;
    }
    printf(" done\n");
    fflush(stdout);


    printf("step 1b: create cublas handle ...");
    fflush(stdout);
    cublas_status = cublasCreate(&cublasH);
    if(cublas_status != CUBLAS_STATUS_SUCCESS)
    {
        printf("CUBLAS initialization failed\n");
        return EXIT_FAILURE;
    }
    printf(" done\n");
    fflush(stdout);





    clock_gettime(CLOCK_REALTIME, &tnow);
    time1sec = 1.0 * ((long) tnow.tv_sec) + 1.0e-9 * tnow.tv_nsec;



    list_image_ID();


    ID_Rmatrix = image_ID(ID_Rmatrix_name);

    datatype = data.image[ID_Rmatrix].md[0].datatype;
    if(datatype != _DATATYPE_FLOAT)
    {
        printf("wrong type\n");
        exit(EXIT_FAILURE);
    }


    uint32_t m;
    uint32_t n;

    if(data.image[ID_Rmatrix].md[0].naxis == 3)
    {
        m = data.image[ID_Rmatrix].md[0].size[0] * data.image[ID_Rmatrix].md[0].size[1];
        n = data.image[ID_Rmatrix].md[0].size[2];
        printf("3D image -> %d %d\n", m, n);
        fflush(stdout);
    }
    else
    {
        m = data.image[ID_Rmatrix].md[0].size[0];
        n = data.image[ID_Rmatrix].md[0].size[1];
        printf("2D image -> %d %d\n", m, n);
        fflush(stdout);
    }

    if(m != n)
    {
        printf("ERROR: m must be equal to n\n");
        exit(EXIT_FAILURE);
    }









    cudaStat = cudaMalloc((void **)&d_A, sizeof(float) * n * m);
    if(cudaStat != cudaSuccess)
    {
        printf("cudaMalloc d_A returned error code %d, line(%d)\n", cudaStat, __LINE__);
        exit(EXIT_FAILURE);
    }

    h_A = (float *) malloc(sizeof(float) * m * n);

    cudaStat = cudaMemcpy(d_A, data.image[ID_Rmatrix].array.F,
                          sizeof(float) * m * n, cudaMemcpyHostToDevice);
    if(cudaStat != cudaSuccess)
    {
        printf("cudaMemcpy d_A returned error code %d, line(%d)\n", cudaStat, __LINE__);
        exit(EXIT_FAILURE);
    }





    cudaStat = cudaMalloc((void **)&d_S, sizeof(float) * n);
    if(cudaStat != cudaSuccess)
    {
        printf("cudaMalloc d_S returned error code %d, line(%d)\n", cudaStat, __LINE__);
        exit(EXIT_FAILURE);
    }

    cudaStat = cudaMalloc((void **)&d_U, sizeof(float) * m * m);
    if(cudaStat != cudaSuccess)
    {
        printf("cudaMalloc d_U returned error code %d, line(%d)\n", cudaStat, __LINE__);
        exit(EXIT_FAILURE);
    }

    cudaStat = cudaMalloc((void **)&d_VT, sizeof(float) * n * n);
    if(cudaStat != cudaSuccess)
    {
        printf("cudaMalloc d_VT returned error code %d, line(%d)\n", cudaStat,
               __LINE__);
        exit(EXIT_FAILURE);
    }

    cudaStat = cudaMalloc((void **)&devInfo, sizeof(int));
    if(cudaStat != cudaSuccess)
    {
        printf("cudaMalloc devInfo returned error code %d, line(%d)\n", cudaStat,
               __LINE__);
        exit(EXIT_FAILURE);
    }

    lda = m;
    ldu = m;
    ldvt = n;
    cusolver_status = cusolverDnSgesvd_bufferSize(cudenseH, m, n, &Lwork);
    if(cusolver_status != CUSOLVER_STATUS_SUCCESS)
    {
        printf("CUSOLVER DnSgesvd_bufferSize failed\n");
        return EXIT_FAILURE;
    }

    cudaStat = cudaMalloc((void **)&d_Work, sizeof(float) * Lwork);
    if(cudaStat != cudaSuccess)
    {
        printf("cudaMalloc d_Work returned error code %d, line(%d)\n", cudaStat,
               __LINE__);
        exit(EXIT_FAILURE);
    }

    rwork = (float *) malloc(5 * sizeof(float) * n);


    printf("START GPU COMPUTATION (%d x %d)  buffer size = %d ...", m, n, Lwork);
    fflush(stdout);
    cusolverDnSgesvd(cudenseH, 'A', 'A', m, n, d_A, lda, d_S, d_U, ldu, d_VT, ldvt,
                     d_Work, Lwork, NULL, devInfo);
    printf(" SYNC ");
    fflush(stdout);
    cudaStat = cudaDeviceSynchronize();
    printf(" DONE\n");
    fflush(stdout);

    cudaStat = cudaMemcpy(&info_gpu, devInfo, sizeof(int), cudaMemcpyDeviceToHost);
    printf("after gesvd: info_gpu = %d\n", info_gpu);


    ID_VTmatrix = create_2Dimage_ID(ID_VTmatrix_name, n, n);
    cudaStat = cudaMemcpy(data.image[ID_VTmatrix].array.F, d_VT,
                          sizeof(float) * n * n, cudaMemcpyDeviceToHost);
    if(cudaStat != cudaSuccess)
    {
        printf("cudaMemcpy returned error code %d, line(%d)\n", cudaStat, __LINE__);
        exit(EXIT_FAILURE);
    }

    save_fits(ID_VTmatrix_name, "!matVT0.fits");


    Sarray = (float *) malloc(sizeof(float) * n);
    //    Aarray = (float*) malloc(sizeof(float)*m*n);
    cudaStat = cudaMemcpy(Sarray, d_S, sizeof(float) * n, cudaMemcpyDeviceToHost);
    if(cudaStat != cudaSuccess)
    {
        printf("cudaMemcpy returned error code %d, line(%d)\n", cudaStat, __LINE__);
        exit(EXIT_FAILURE);
    }

    sprintf(fname, "eigenv.dat.gsl");
    if((fp = fopen(fname, "w")) == NULL)
    {
        printf("ERROR: cannot create file \"%s\"\n", fname);
        exit(0);
    }
    for(i = 0; i < n; i++)
    {
        fprintf(fp, "%5ld %20g %20g\n", i, Sarray[i], Sarray[i] / Sarray[0]);
    }
    fclose(fp);



    ID = create_2Dimage_ID("matU", m, m);
    cudaMemcpy(data.image[ID].array.F, d_U, sizeof(float)*m * m,
               cudaMemcpyDeviceToHost);
    save_fits("matU", "!matU.fits");

    h_U1 = (float *) malloc(sizeof(float) * m * n);
    cudaStat = cudaMalloc((void **)&d_U1, sizeof(float) * m * n);
    if(cudaStat != cudaSuccess)
    {
        printf("cudaMalloc d_U1 returned error code %d, line(%d)\n", cudaStat,
               __LINE__);
        exit(EXIT_FAILURE);
    }
    for(uint32_t ii = 0; ii < m; ii++)
        for(uint32_t jj = 0; jj < n; jj++)
        {
            h_U1[jj * m + ii] = data.image[ID].array.F[jj * m + ii];
        }
    cudaMemcpy(d_U1, h_U1, sizeof(float)*m * n, cudaMemcpyHostToDevice);
    free(h_U1);

    ID = create_2Dimage_ID("matU1", m, n);
    cudaMemcpy(data.image[ID].array.F, d_U1, sizeof(float)*m * n,
               cudaMemcpyDeviceToHost);
    save_fits("matU1", "!matU1.fits");




    printf("SVDeps = %f\n", SVDeps);
    cnt0 = 0;
    // multiply lines of VT by 1/eigenval
    for(uint32_t ii = 0; ii < n; ii++)
    {
        if(Sarray[ii] > Sarray[0]*SVDeps)
        {
            val = 1.0 / (Sarray[ii]);
            cnt0++;
        }
        else
        {
            val = 0.0;
        }

        for(uint32_t jj = 0; jj < n; jj++)
        {
            data.image[ID_VTmatrix].array.F[jj * n + ii] *= val;
        }
    }
    printf("%ld eigenvalues kept\n", cnt0);

    // copy VT back to GPU
    cudaStat = cudaMemcpy(d_VT, data.image[ID_VTmatrix].array.F,
                          sizeof(float) * n * n, cudaMemcpyHostToDevice);
    if(cudaStat != cudaSuccess)
    {
        printf("cudaMemcpy returned error code %d, line(%d)\n", cudaStat, __LINE__);
        exit(EXIT_FAILURE);
    }


    cudaStat = cudaMalloc((void **)&d_M, sizeof(float) * n * m);
    if(cudaStat != cudaSuccess)
    {
        printf("cudaMalloc d_M returned error code %d, line(%d)\n", cudaStat, __LINE__);
        exit(EXIT_FAILURE);
    }


    save_fits(ID_VTmatrix_name, "!matVT.fits");

    cudaStat = cublasSgemm(cublasH, CUBLAS_OP_T, CUBLAS_OP_T, n, m, n, &alpha, d_VT,
                           n, d_U, m, &beta, d_M, n);
    if(cudaStat != cudaSuccess)
    {
        printf("cublasSgemm returned error code %d, line(%d)\n", cudaStat, __LINE__);
        exit(EXIT_FAILURE);
    }



    arraysizetmp = (uint32_t *) malloc(sizeof(uint32_t) * 3);

    if(data.image[ID_Rmatrix].md[0].naxis == 3)
    {
        arraysizetmp[0] = data.image[ID_Rmatrix].md[0].size[0];
        arraysizetmp[1] = data.image[ID_Rmatrix].md[0].size[1];
        arraysizetmp[2] = n;
    }
    else
    {
        arraysizetmp[0] = m;
        arraysizetmp[1] = n;
    }


    ID_Cmatrix = create_image_ID(ID_Cmatrix_name,
                                 data.image[ID_Rmatrix].md[0].naxis, arraysizetmp, _DATATYPE_FLOAT, 0, 0);


//   cudaStat = cudaMemcpy(data.image[ID_Cmatrix].array.F, d_M, sizeof(float)*m*n, cudaMemcpyDeviceToHost);

    h_M = (float *) malloc(sizeof(float) * m * n);
    cudaStat = cudaMemcpy(h_M, d_M, sizeof(float) * m * n, cudaMemcpyDeviceToHost);
    for(uint32_t ii = 0; ii < m; ii++)
        for(uint32_t jj = 0; jj < n; jj++)
        {
            data.image[ID_Cmatrix].array.F[jj * m + ii] = h_M[ii * n + jj];
        }

    //cudaStat = cudaMemcpy(data.image[ID_Cmatrix].array.F, d_VT, sizeof(float)*n*n, cudaMemcpyDeviceToHost);
    if(cudaStat != cudaSuccess)
    {
        printf("cudaMemcpy returned error code %d, line(%d)\n", cudaStat, __LINE__);
        free(arraysizetmp);
        exit(EXIT_FAILURE);
    }



    cudaFree(d_A);
    cudaFree(d_S);
    cudaFree(d_U);
    cudaFree(d_VT);
    cudaFree(d_Work);
    cudaFree(devInfo);
    cudaFree(d_M);
    cudaFree(d_U1);

    clock_gettime(CLOCK_REALTIME, &tnow);
    time2sec = 1.0 * ((long) tnow.tv_sec) + 1.0e-9 * tnow.tv_nsec;

    printf("time = %8.3f s\n", 1.0 * (time2sec - time1sec));




    if(cublasH)
    {
        cublasDestroy(cublasH);
    }
    if(cudenseH)
    {
        cusolverDnDestroy(cudenseH);
    }

    cudaDeviceReset();

    free(arraysizetmp);
    free(Sarray);
    free(rwork);
    free(h_A);
    free(h_M);


    return RETURN_SUCCESS;
}













/* =============================================================================================== */
/* =============================================================================================== */
/*                                                                                                 */
/* 4. HIGH LEVEL FUNCTIONS                                                                         */
/*                                                                                                 */
/* =============================================================================================== */
/* =============================================================================================== */







//
// single GPU
// semaphore input = 3
//
errno_t CUDACOMP_Coeff2Map_Loop(
    const char *IDmodes_name,
    const char *IDcoeff_name,
    int         GPUindex,
    const char *IDoutmap_name,
    int         offsetmode,
    const char *IDoffset_name
)
{
    long     NBmodes;
    imageID  IDmodes;
    imageID  IDcoeff;
    imageID  IDoutmap;

    cublasHandle_t   cublasH       = NULL;
    cublasStatus_t   cublas_status = CUBLAS_STATUS_SUCCESS;
    cudaError_t      cudaStat      = cudaSuccess;
    struct cudaDeviceProp deviceProp;

    float *d_modes  = NULL; // linear memory of GPU
    float *d_coeff  = NULL;
    float *d_outmap = NULL;

    float alpha = 1.0;
    float beta = 0.0;
    int loopOK;
    struct timespec ts;
    long iter;
    uint64_t cnt;
    long scnt;
    int semval;
    int semr;

    imageID IDoffset;


    printf("entering CUDACOMP_Coeff2Map_Loop\n");
    printf("offsetmode = %d\n", offsetmode);
    fflush(stdout);

    if(offsetmode == 1)
    {
        beta = 1.0;
        IDoffset = image_ID(IDoffset_name);

        if(IDoffset == -1)
        {
            printf("ERROR: image \"%s\" does not exist\n", IDoffset_name);
            exit(0);
        }
    }




    IDoutmap = image_ID(IDoutmap_name);
    if(IDoutmap == -1)
    {
        printf("ERROR: missing output stream\n");
        exit(0);
    }
    COREMOD_MEMORY_image_set_createsem(IDoutmap_name, 5);


    cudaGetDeviceCount(&deviceCount);
    printf("%d devices found\n", deviceCount);
    fflush(stdout);
    printf("\n");
    for(int k = 0; k < deviceCount; ++k)
    {
        cudaGetDeviceProperties(&deviceProp, k);
        printf("Device %d [ %20s ]  has compute capability %d.%d.\n",
               k, deviceProp.name, deviceProp.major, deviceProp.minor);
        printf("  Total amount of global memory:                 %.0f MBytes (%llu bytes)\n",
               (float)deviceProp.totalGlobalMem / 1048576.0f,
               (unsigned long long) deviceProp.totalGlobalMem);
        printf("  (%2d) Multiprocessors\n", deviceProp.multiProcessorCount);
        printf("  GPU Clock rate:                                %.0f MHz (%0.2f GHz)\n",
               deviceProp.clockRate * 1e-3f, deviceProp.clockRate * 1e-6f);
        printf("\n");
    }


    if(GPUindex < deviceCount)
    {
        cudaSetDevice(GPUindex);
    }
    else
    {
        printf("Invalid Device : %d / %d\n", GPUindex, deviceCount);
        exit(0);
    }


    printf("Create cublas handle ...");
    fflush(stdout);
    cublas_status = cublasCreate(&cublasH);
    if(cublas_status != CUBLAS_STATUS_SUCCESS)
    {
        printf("CUBLAS initialization failed\n");
        return EXIT_FAILURE;
    }
    printf(" done\n");
    fflush(stdout);





    // load modes to GPU

    IDcoeff = image_ID(IDcoeff_name);
    NBmodes = 1;
    for(uint8_t k = 0; k < data.image[IDcoeff].md[0].naxis; k++)
    {
        NBmodes *= data.image[IDcoeff].md[0].size[k];
    }

    IDmodes = image_ID(IDmodes_name);
    uint64_t mdim;
    if(data.image[IDmodes].md[0].naxis == 3)
    {
        mdim = data.image[IDmodes].md[0].size[0] * data.image[IDmodes].md[0].size[1];
    }
    else
    {
        mdim = data.image[IDmodes].md[0].size[0];
    }


    printf("Allocating d_modes. Size = %lu x %ld, total = %ld\n", mdim, NBmodes,
           sizeof(float)*mdim * NBmodes);
    fflush(stdout);
    cudaStat = cudaMalloc((void **)&d_modes, sizeof(float) * mdim * NBmodes);
    if(cudaStat != cudaSuccess)
    {
        printf("cudaMalloc d_DMmodes returned error code %d, line(%d)\n", cudaStat,
               __LINE__);
        exit(EXIT_FAILURE);
    }

    printf("cudaMemcpy ID %ld  -> d_modes\n", IDmodes);
    fflush(stdout);
    list_image_ID();
    cudaStat = cudaMemcpy(d_modes, data.image[IDmodes].array.F,
                          sizeof(float) * mdim * NBmodes, cudaMemcpyHostToDevice);
    if(cudaStat != cudaSuccess)
    {
        printf("cudaMemcpy returned error code %d, line(%d)\n", cudaStat, __LINE__);
        exit(EXIT_FAILURE);
    }


    // create d_outmap
    printf("Allocating d_outmap. Size = %ld,  total = %ld\n", mdim,
           sizeof(float)*mdim);
    fflush(stdout);
    cudaStat = cudaMalloc((void **)&d_outmap, sizeof(float) * mdim);
    if(cudaStat != cudaSuccess)
    {
        printf("cudaMalloc d_outmap returned error code %d, line(%d)\n", cudaStat,
               __LINE__);
        exit(EXIT_FAILURE);
    }


    // create d_coeff
    printf("Allocating d_coeff. Size = %ld,  total = %ld\n", NBmodes,
           sizeof(float)*NBmodes);
    fflush(stdout);
    cudaStat = cudaMalloc((void **)&d_coeff, sizeof(float) * NBmodes);
    if(cudaStat != cudaSuccess)
    {
        printf("cudaMalloc d_coeff returned error code %d, line(%d)\n", cudaStat,
               __LINE__);
        exit(EXIT_FAILURE);
    }


    if(sigaction(SIGINT, &data.sigact, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    if(sigaction(SIGTERM, &data.sigact, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    if(sigaction(SIGBUS, &data.sigact, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    if(sigaction(SIGSEGV, &data.sigact, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    if(sigaction(SIGABRT, &data.sigact, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    if(sigaction(SIGHUP, &data.sigact, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    if(sigaction(SIGPIPE, &data.sigact, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    if(sigaction(SIGSEGV, &data.sigact, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }



    loopOK = 1;
    iter = 0;

    printf("ENTERING LOOP, %ld modes (offsetmode = %d)\n", NBmodes, offsetmode);
    fflush(stdout);

    while(loopOK == 1)
    {

        if(data.image[IDcoeff].md[0].sem == 0)
        {
            while(data.image[IDcoeff].md[0].cnt0 == cnt)   // test if new frame exists
            {
                struct timespec treq, trem;
                treq.tv_sec = 0;
                treq.tv_nsec = 5000;
                nanosleep(&treq, &trem);
            }
            cnt = data.image[IDcoeff].md[0].cnt0;
            semr = 0;
        }
        else
        {


            if(clock_gettime(CLOCK_REALTIME, &ts) == -1)
            {
                perror("clock_gettime");
                exit(EXIT_FAILURE);
            }
            ts.tv_sec += 1;
            semr = sem_timedwait(data.image[IDcoeff].semptr[3], &ts);


            if(iter == 0)
            {
                //  printf("driving semaphore to zero ... ");
                // fflush(stdout);
                sem_getvalue(data.image[IDcoeff].semptr[2], &semval);
                for(scnt = 0; scnt < semval; scnt++)
                {
                    printf("WARNING %s %d  : sem_trywait on semptr2\n", __FILE__, __LINE__);
                    fflush(stdout);
                    sem_trywait(data.image[IDcoeff].semptr[2]);
                }
                // printf("done\n");
                // fflush(stdout);
            }
        }




        if(semr == 0)
        {
            //  printf("Compute\n");
            //  fflush(stdout);

            // send vector back to GPU
            cudaStat = cudaMemcpy(d_coeff, data.image[IDcoeff].array.F,
                                  sizeof(float) * NBmodes, cudaMemcpyHostToDevice);
            if(cudaStat != cudaSuccess)
            {
                printf("cudaMemcpy returned error code %d, line(%d)\n", cudaStat, __LINE__);
                exit(EXIT_FAILURE);
            }

            if(offsetmode == 1)
            {
                cudaStat = cudaMemcpy(d_outmap, data.image[IDoffset].array.F,
                                      sizeof(float) * mdim, cudaMemcpyHostToDevice);
                if(cudaStat != cudaSuccess)
                {
                    printf("cudaMemcpy returned error code %d, line(%d)\n", cudaStat, __LINE__);
                    exit(EXIT_FAILURE);
                }
            }

            // compute
            cublas_status = cublasSgemv(cublasH, CUBLAS_OP_N, mdim, NBmodes, &alpha,
                                        d_modes, mdim, d_coeff, 1, &beta, d_outmap, 1);
            if(cublas_status != CUBLAS_STATUS_SUCCESS)
            {
                printf("cublasSgemv returned error code %d, line(%d)\n", cublas_status,
                       __LINE__);
                fflush(stdout);
                if(cublas_status == CUBLAS_STATUS_NOT_INITIALIZED)
                {
                    printf("   CUBLAS_STATUS_NOT_INITIALIZED\n");
                }
                if(cublas_status == CUBLAS_STATUS_INVALID_VALUE)
                {
                    printf("   CUBLAS_STATUS_INVALID_VALUE\n");
                }
                if(cublas_status == CUBLAS_STATUS_ARCH_MISMATCH)
                {
                    printf("   CUBLAS_STATUS_ARCH_MISMATCH\n");
                }
                if(cublas_status == CUBLAS_STATUS_EXECUTION_FAILED)
                {
                    printf("   CUBLAS_STATUS_EXECUTION_FAILED\n");
                }

                printf("GPU index                           = %d\n", GPUindex);

                printf("CUBLAS_OP_N                         = %d\n", CUBLAS_OP_N);
                printf("alpha                               = %f\n", alpha);
                printf("alpha                               = %f\n", beta);
                printf("m                                   = %d\n", (int) mdim);
                printf("NBmodes                             = %d\n", (int) NBmodes);
                fflush(stdout);
                exit(EXIT_FAILURE);
            }

            // copy result
            data.image[IDoutmap].md[0].write = 1;
            cudaStat = cudaMemcpy(data.image[IDoutmap].array.F, d_outmap,
                                  sizeof(float) * mdim, cudaMemcpyDeviceToHost);
            sem_getvalue(data.image[IDoutmap].semptr[0], &semval);
            if(semval < SEMAPHORE_MAXVAL)
            {
                sem_post(data.image[IDoutmap].semptr[0]);
            }
            sem_getvalue(data.image[IDoutmap].semptr[1], &semval);
            if(semval < SEMAPHORE_MAXVAL)
            {
                sem_post(data.image[IDoutmap].semptr[1]);
            }
            data.image[IDoutmap].md[0].cnt0++;
            data.image[IDoutmap].md[0].write = 0;



        }

        if((data.signal_INT == 1) || (data.signal_TERM == 1) || (data.signal_ABRT == 1)
                || (data.signal_BUS == 1) || (data.signal_SEGV == 1) || (data.signal_HUP == 1)
                || (data.signal_PIPE == 1))
        {
            loopOK = 0;
        }

        iter++;

    }


    cudaFree(d_modes);
    cudaFree(d_outmap);
    cudaFree(d_coeff);


    if(cublasH)
    {
        cublasDestroy(cublasH);
    }



    return RETURN_SUCCESS;
}
















// extract mode coefficients from data stream
/*
int CUDACOMP_createModesLoop(const char *DMmodeval_stream, const char *DMmodes, const char *DMact_stream, int GPUindex)
{
    long ID_DMmodeval;
    long ID_DMmodes;
    long ID_DMact;
    cublasHandle_t cublasH = NULL;
    cublasStatus_t cublas_status = CUBLAS_STATUS_SUCCESS;
    cudaError_t cudaStat = cudaSuccess;
    struct cudaDeviceProp deviceProp;
    int m, n;
    int k;
    long *arraytmp;

    float *d_DMmodes = NULL; // linear memory of GPU
    float *d_DMact = NULL;
    float *d_modeval = NULL;

    float alpha = 1.0;
    float beta = 0.0;
    int loopOK;
    struct timespec ts;
    long iter;
    long long cnt = -1;
    long scnt;
    int semval;
    int semr;
    long ii, kk;

    long NBmodes;

    float *normcoeff;



    ID_DMact = image_ID(DMact_stream);
    m = data.image[ID_DMact].md[0].size[0]*data.image[ID_DMact].md[0].size[1];

    ID_DMmodes = image_ID(DMmodes);
    n = data.image[ID_DMmodes].md[0].size[2];
    NBmodes = n;
    normcoeff = (float*) malloc(sizeof(float)*NBmodes);

    for(kk=0;kk<NBmodes;kk++)
        {
            normcoeff[kk] = 0.0;
            for(ii=0;ii<m;ii++)
                normcoeff[kk] += data.image[ID_DMmodes].array.F[kk*m+ii]*data.image[ID_DMmodes].array.F[kk*m+ii];
            for(ii=0;ii<m;ii++)
                data.image[ID_DMmodes].array.F[kk*m+ii] /= normcoeff[kk];
        }

    //NBmodes = 3;

    arraytmp = (long*) malloc(sizeof(long)*2);
    arraytmp[0] = NBmodes;
    arraytmp[1] = 1;
    ID_modeval = create_image_ID(DMmodes_val, 2, arraytmp, _DATATYPE_FLOAT, 1, 0);
    free(arraytmp);
    COREMOD_MEMORY_image_set_createsem(DMmodes_val, 2);


    cudaGetDeviceCount(&deviceCount);
    printf("%d devices found\n", deviceCount);
    fflush(stdout);
    printf("\n");
    for (k = 0; k < deviceCount; ++k) {
        cudaGetDeviceProperties(&deviceProp, k);
        printf("Device %d [ %20s ]  has compute capability %d.%d.\n",
               k, deviceProp.name, deviceProp.major, deviceProp.minor);
        printf("  Total amount of global memory:                 %.0f MBytes (%llu bytes)\n", (float)deviceProp.totalGlobalMem/1048576.0f, (unsigned long long) deviceProp.totalGlobalMem);
        printf("  (%2d) Multiprocessors\n", deviceProp.multiProcessorCount);
        printf("  GPU Clock rate:                                %.0f MHz (%0.2f GHz)\n", deviceProp.clockRate * 1e-3f, deviceProp.clockRate * 1e-6f);
        printf("\n");
    }


    if(GPUindex<deviceCount)
        cudaSetDevice(GPUindex);
    else
    {
        printf("Invalid Device : %d / %d\n", GPUindex, deviceCount);
        exit(0);
    }


    printf("Create cublas handle ...");
    fflush(stdout);
    cublas_status = cublasCreate(&cublasH);
    if (cublas_status != CUBLAS_STATUS_SUCCESS) {
        printf ("CUBLAS initialization failed\n");
        return EXIT_FAILURE;
    }
    printf(" done\n");
    fflush(stdout);


    // load DMmodes to GPU
    cudaStat = cudaMalloc((void**)&d_DMmodes, sizeof(float)*m*NBmodes);
    if (cudaStat != cudaSuccess)
    {
        printf("cudaMalloc d_DMmodes returned error code %d, line(%d)\n", cudaStat, __LINE__);
        exit(EXIT_FAILURE);
    }
    cudaStat = cudaMemcpy(d_DMmodes, data.image[ID_DMmodes].array.F, sizeof(float)*m*NBmodes, cudaMemcpyHostToDevice);
    if (cudaStat != cudaSuccess)
    {
        printf("cudaMemcpy returned error code %d, line(%d)\n", cudaStat, __LINE__);
        exit(EXIT_FAILURE);
    }


    // create d_DMact
    cudaStat = cudaMalloc((void**)&d_DMact, sizeof(float)*m);
    if (cudaStat != cudaSuccess)
    {
        printf("cudaMalloc d_DMact returned error code %d, line(%d)\n", cudaStat, __LINE__);
        exit(EXIT_FAILURE);
    }

    // create d_modeval
    cudaStat = cudaMalloc((void**)&d_modeval, sizeof(float)*NBmodes);
    if (cudaStat != cudaSuccess)
    {
        printf("cudaMalloc d_modeval returned error code %d, line(%d)\n", cudaStat, __LINE__);
        exit(EXIT_FAILURE);
    }


    if (sigaction(SIGINT, &data.sigact, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    if (sigaction(SIGTERM, &data.sigact, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    if (sigaction(SIGBUS, &data.sigact, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    if (sigaction(SIGSEGV, &data.sigact, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    if (sigaction(SIGABRT, &data.sigact, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    if (sigaction(SIGHUP, &data.sigact, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    if (sigaction(SIGPIPE, &data.sigact, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }


    loopOK = 1;
    iter = 0;

    while(loopOK == 1)
    {
        if(data.image[ID_DMact].md[0].sem==0)
        {
            while(data.image[ID_DMact].md[0].cnt0==cnt) // test if new frame exists
                usleep(5);
            cnt = data.image[ID_DMact].md[0].cnt0;
            semr = 0;
        }
        else
        {
            if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
                perror("clock_gettime");
                exit(EXIT_FAILURE);
            }
            ts.tv_sec += 1;
            semr = sem_timedwait(data.image[ID_DMact].semptr[0], &ts);

            if(iter == 0)
            {
                printf("driving semaphore to zero ... ");
                fflush(stdout);
                sem_getvalue(data.image[ID_DMact].semptr[0], &semval);
                for(scnt=0; scnt<semval; scnt++)
                    sem_trywait(data.image[ID_DMact].semptr[0]);
                printf("done\n");
                fflush(stdout);
            }
        }

        if(semr==0)
        {

            // load DMact to GPU
            cudaStat = cudaMemcpy(d_DMact, data.image[ID_DMact].array.F, sizeof(float)*m, cudaMemcpyHostToDevice);
            if (cudaStat != cudaSuccess)
            {
                printf("cudaMemcpy returned error code %d, line(%d)\n", cudaStat, __LINE__);
                exit(EXIT_FAILURE);
            }

            // compute
            cublas_status = cublasSgemv(cublasH, CUBLAS_OP_T, m, NBmodes, &alpha, d_DMmodes, m, d_DMact, 1, &beta, d_modeval, 1);
            if (cudaStat != CUBLAS_STATUS_SUCCESS)
            {
                printf("cublasSgemv returned error code %d, line(%d)\n", stat, __LINE__);
                if(stat == CUBLAS_STATUS_NOT_INITIALIZED)
                    printf("   CUBLAS_STATUS_NOT_INITIALIZED\n");
                if(stat == CUBLAS_STATUS_INVALID_VALUE)
                    printf("   CUBLAS_STATUS_INVALID_VALUE\n");
                if(stat == CUBLAS_STATUS_ARCH_MISMATCH)
                    printf("   CUBLAS_STATUS_ARCH_MISMATCH\n");
                if(stat == CUBLAS_STATUS_EXECUTION_FAILED)
                    printf("   CUBLAS_STATUS_EXECUTION_FAILED\n");
                exit(EXIT_FAILURE);
            }

            // copy result
            data.image[ID_modeval].md[0].write = 1;
            cudaStat = cudaMemcpy(data.image[ID_modeval].array.F, d_modeval, sizeof(float)*NBmodes, cudaMemcpyDeviceToHost);
            sem_getvalue(data.image[ID_modeval].semptr[0], &semval);
            if(semval<SEMAPHORE_MAXVAL)
                sem_post(data.image[ID_modeval].semptr[0]);
            sem_getvalue(data.image[ID_modeval].semptr[1], &semval);
            if(semval<SEMAPHORE_MAXVAL)
                sem_post(data.image[ID_modeval].semptr[1]);
            data.image[ID_modeval].md[0].cnt0++;
            data.image[ID_modeval].md[0].write = 0;
        }

        if((data.signal_INT == 1)||(data.signal_TERM == 1)||(data.signal_ABRT==1)||(data.signal_BUS==1)||(data.signal_SEGV==1)||(data.signal_HUP==1)||(data.signal_PIPE==1))
            loopOK = 0;

        iter++;
    }


    cudaFree(d_DMmodes);
    cudaFree(d_DMact);
    cudaFree(d_modeval);

    if (cublasH ) cublasDestroy(cublasH);

    free(normcoeff);

    return(0);
}

*/


























#endif










