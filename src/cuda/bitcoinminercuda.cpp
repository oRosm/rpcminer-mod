/**
    Copyright (C) 2010  puddinpop

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
**/

#define NOMINMAX

#include "bitcoinminercuda.h"
#include "cudashared.h"
#include "../cryptopp/sha.h"	// for CryptoPP::ByteReverse
//#include <cutil_inline.h>
#include <limits>

#define ALIGN_UP(offset, alignment) \
	(offset) = ((offset) + (alignment) - 1) & ~((alignment) - 1)

CUDARunner::CUDARunner():GPURunner<unsigned long,int>(TYPE_CUDA)
{
	m_in=0;
	m_devin=0;
	m_out=0;
	m_devout=0;
	CUresult rval;
	int major=0;
	int minor=0;
	std::string cuda_module_path("bitcoinminercuda.ptx");

	rval=cuInit(0);

	if(rval==CUDA_SUCCESS)
	{
		rval=cuDeviceGetCount(&m_devicecount);

		printf("%d CUDA GPU devices found\n",m_devicecount);

		if(m_devicecount>0)
		{
			if(m_deviceindex>=0 && m_deviceindex<m_devicecount)
			{
				printf("Setting CUDA device to device %d\n",m_deviceindex);
				rval=cuDeviceGet(&m_device,m_deviceindex);
				if(rval!=CUDA_SUCCESS)
				{
					exit(0);
				}
			}
			else
			{
				m_deviceindex=0;
				printf("Setting CUDA device to first device found\n");
				rval=cuDeviceGet(&m_device,0);
				if(rval!=CUDA_SUCCESS)
				{
					exit(0);
				}
			}

			cuDeviceComputeCapability(&major, &minor, m_device);

			rval=cuCtxCreate(&m_context,CU_CTX_BLOCKING_SYNC,m_device);
			if(rval!=CUDA_SUCCESS)
			{
				printf("Unable to create CUDA context\n");
				exit(0);
			}

			printf("Loading module %s\n",cuda_module_path.c_str());
			rval=cuModuleLoad(&m_module,cuda_module_path.c_str());
			if(rval!=CUDA_SUCCESS)
			{
				printf("Unable to load CUDA module: %i\n", rval);
				cuCtxDestroy(m_context);
				exit(0);
			}

			rval=cuModuleGetFunction(&m_function,m_module,"cuda_process");
			if(rval!=CUDA_SUCCESS)
			{
				printf("Unable to get function cuda_process %d\n",rval);
				cuModuleUnload(m_module);
				cuCtxDestroy(m_context);
				exit(0);
			}

			printf("CUDA initialized\n");

		}
		else
		{
			printf("No CUDA capable devices found\n");
			exit(0);
		}
	}
	else
	{
		printf("Unable to initialize CUDA\n");
		exit(0);
	}
}

CUDARunner::~CUDARunner()
{
	DeallocateResources();

	cuModuleUnload(m_module);
	cuCtxDestroy(m_context);
}

const bool CUDARunner::AllocateResources(const int numb, const int numt)
{
	bool allocated=true;
	CUresult rval;
	DeallocateResources();

	m_in=(cuda_in *)malloc(sizeof(cuda_in));
	m_out=(cuda_out *)malloc(numb*numt*sizeof(cuda_out));

	rval=cuMemAlloc(&m_devin,sizeof(cuda_in));
	if(rval!=CUDA_SUCCESS)
	{
		printf("Error %d allocating CUDA memory\n",rval);
		m_devin=0;
		allocated=false;
	}
	rval=cuMemAlloc(&m_devout,numb*numt*sizeof(cuda_out));
	if(rval!=CUDA_SUCCESS)
	{
		printf("Error %d allocating CUDA memory\n",rval);
		m_devout=0;
		allocated=false;
	}

	printf("Done allocating CUDA resources for (%d,%d)\n",numb,numt);
	return allocated;
}

void CUDARunner::DeallocateResources()
{
	if(m_in)
	{
		free(m_in);
		m_in=0;
	}
	if(m_devin)
	{
		cuMemFree(m_devin);
		m_devin=0;
	}
	if(m_out)
	{
		free(m_out);
		m_out=0;
	}
	if(m_devout)
	{
		cuMemFree(m_devout);
		m_devout=0;
	}
}


void CUDARunner::FindBestConfiguration()
{
	unsigned long lowb=16;
	unsigned long highb=128;
	unsigned long lowt=16;
	unsigned long hight=256;
	unsigned long bestb=16;
	unsigned long bestt=16;
	int offset=0;
	void *ptr=0;
	int64 besttime=std::numeric_limits<int64>::max();

	if(m_requestedgrid>0 && m_requestedgrid<=65536)
	{
		lowb=m_requestedgrid;
		highb=m_requestedgrid;
	}

	if(m_requestedthreads>0 && m_requestedthreads<=65536)
	{
		lowt=m_requestedthreads;
		hight=m_requestedthreads;
	}

	for(int numb=lowb; numb<=highb; numb*=2)
	{
		for(int numt=lowt; numt<=hight; numt*=2)
		{
			if(AllocateResources(numb,numt)==true)
			{
				// clear out any existing error
				CUresult err=CUDA_SUCCESS;

				int64 st=GetTimeMillis();

				for(int it=0; it<128*256*2 && err==CUDA_SUCCESS; it+=(numb*numt))
				{

					cuMemcpyHtoD(m_devin,m_in,sizeof(cuda_in));

					offset=0;
					int loops=64;
					int bits=5;

					ptr=(void *)(size_t)m_devin;
					ALIGN_UP(offset, __alignof(ptr));
					cuParamSetv(m_function,offset,&ptr,sizeof(ptr));
					offset+=sizeof(ptr);

					ptr=(void *)(size_t)m_devout;
					ALIGN_UP(offset, __alignof(ptr));
					cuParamSetv(m_function,offset,&ptr,sizeof(ptr));
					offset+=sizeof(ptr);

					ALIGN_UP(offset, __alignof(loops));
					cuParamSeti(m_function,offset,loops);
					offset+=sizeof(loops);

					ALIGN_UP(offset, __alignof(bits));
					cuParamSeti(m_function,offset,bits);
					offset+=sizeof(bits);

					cuParamSetSize(m_function,offset);

					err=cuFuncSetBlockShape(m_function,numt,1,1);
					if(err!=CUDA_SUCCESS)
					{
						printf("cuFuncSetBlockShape error %d\n",err);
						continue;
					}

					err=cuLaunchGrid(m_function,numb,1);
					if(err!=CUDA_SUCCESS)
					{
						printf("cuLaunchGrid error %d\n",err);
						continue;
					}

					cuMemcpyDtoH(m_out,m_devout,numt*numb*sizeof(cuda_out));

					if(err!=CUDA_SUCCESS)
					{
						printf("CUDA error %d\n",err);
					}
				}

				int64 et=GetTimeMillis();

				printf("Finding best configuration step end (%d,%d) %"PRI64d"ms  prev best=%"PRI64d"ms\n",numb,numt,et-st,besttime);

				if((et-st)<besttime && err==CUDA_SUCCESS)
				{
					bestb=numb;
					bestt=numt;
					besttime=et-st;
				}
			}
		}
	}

	m_numb=bestb;
	m_numt=bestt;

	AllocateResources(m_numb,m_numt);

}


const unsigned long CUDARunner::RunStep()
{
	//unsigned int best=0;
	//unsigned int bestg=~0;
	int offset=0;

	if(m_in==0 || m_out==0 || m_devin==0 || m_devout==0)
	{
		AllocateResources(m_numb,m_numt);
	}
	m_out[0].m_bestnonce=0;
	cuMemcpyHtoD(m_devout,m_out,/*m_numb*m_numt*/sizeof(cuda_out));

	cuMemcpyHtoD(m_devin,m_in,sizeof(cuda_in));

	int loops=GetStepIterations();
	int bits=GetStepBitShift()-1;

	void *ptr=(void *)(size_t)m_devin;
	ALIGN_UP(offset, __alignof(ptr));
	cuParamSetv(m_function,offset,&ptr,sizeof(ptr));
	offset+=sizeof(ptr);

	ptr=(void *)(size_t)m_devout;
	ALIGN_UP(offset, __alignof(ptr));
	cuParamSetv(m_function,offset,&ptr,sizeof(ptr));
	offset+=sizeof(ptr);

	ALIGN_UP(offset, __alignof(loops));
	cuParamSeti(m_function,offset,loops);
	offset+=sizeof(loops);

	ALIGN_UP(offset, __alignof(bits));
	cuParamSeti(m_function,offset,bits);
	offset+=sizeof(bits);

	cuParamSetSize(m_function,offset);

	cuFuncSetBlockShape(m_function,m_numt,1,1);
	cuLaunchGrid(m_function,m_numb,1);

	cuMemcpyDtoH(m_out,m_devout,/*m_numb*m_numt*/sizeof(cuda_out));

	// very unlikely that we will find more than 1 hash with H=0
	// so we'll just return the first one and not even worry about G
	for(int i=0; i<1/*m_numb*m_numt*/; i++)
	{
		if(m_out[i].m_bestnonce!=0)// && m_out[i].m_bestg<bestg)
		{
			return CryptoPP::ByteReverse(m_out[i].m_bestnonce);
			//best=m_out[i].m_bestnonce;
			//bestg=m_out[i].m_bestg;
		}
	}

	return 0;

}

/*
CUDARunner::CUDARunner():GPURunner<unsigned long,int>(TYPE_CUDA)
{
	m_in=0;
	m_devin=0;
	m_out=0;
	m_devout=0;

	cutilSafeCall(cudaGetDeviceCount(&m_devicecount));

	if(m_devicecount>0)
	{
		if(m_deviceindex<0 || m_deviceindex>=m_devicecount)
		{
			m_deviceindex=cutGetMaxGflopsDeviceId();
			printf("Setting CUDA device to Max GFlops device at index %u\n",m_deviceindex);
		}
		else
		{
			printf("Setting CUDA device to device at index %u\n",m_deviceindex);
		}
		
		cudaDeviceProp props;
		cudaGetDeviceProperties(&props,m_deviceindex);

		printf("Device info for %s :\nCompute Capability : %d.%d\nClock Rate (hz) : %d\n",props.name,props.major,props.minor,props.clockRate);

		if(props.major>999)
		{
			printf("CUDA seems to be running in CPU emulation mode\n");
		}

		cutilSafeCall(cudaSetDevice(m_deviceindex));

	}
	else
	{
		m_deviceindex=-1;
		printf("No CUDA capable device detected\n");
	}
}

CUDARunner::~CUDARunner()
{
	DeallocateResources();
	cutilSafeCall(cudaThreadExit());
}

void CUDARunner::AllocateResources(const int numb, const int numt)
{
	DeallocateResources();

	m_in=(cuda_in *)malloc(sizeof(cuda_in));
	m_out=(cuda_out *)malloc(numb*numt*sizeof(cuda_out));

	cutilSafeCall(cudaMalloc((void **)&m_devin,sizeof(cuda_in)));
	cutilSafeCall(cudaMalloc((void **)&m_devout,numb*numt*sizeof(cuda_out)));

	printf("Done allocating CUDA resources for (%d,%d)\n",numb,numt);
}

void CUDARunner::DeallocateResources()
{
	if(m_in)
	{
		free(m_in);
		m_in=0;
	}
	if(m_devin)
	{
		cutilSafeCall(cudaFree(m_devin));
		m_devin=0;
	}
	if(m_out)
	{
		free(m_out);
		m_out=0;
	}
	if(m_devout)
	{
		cutilSafeCall(cudaFree(m_devout));
		m_devout=0;
	}
}

void CUDARunner::FindBestConfiguration()
{
	unsigned long lowb=16;
	unsigned long highb=128;
	unsigned long lowt=16;
	unsigned long hight=256;
	unsigned long bestb=16;
	unsigned long bestt=16;
	int64 besttime=std::numeric_limits<int64>::max();

	if(m_requestedgrid>0 && m_requestedgrid<=65536)
	{
		lowb=m_requestedgrid;
		highb=m_requestedgrid;
	}

	if(m_requestedthreads>0 && m_requestedthreads<=65536)
	{
		lowt=m_requestedthreads;
		hight=m_requestedthreads;
	}

	for(int numb=lowb; numb<=highb; numb*=2)
	{
		for(int numt=lowt; numt<=hight; numt*=2)
		{
			AllocateResources(numb,numt);
			// clear out any existing error
			cudaError_t err=cudaGetLastError();
			err=cudaSuccess;

			int64 st=GetTimeMillis();

			for(int it=0; it<128*256*2 && err==0; it+=(numb*numt))
			{
				cutilSafeCall(cudaMemcpy(m_devin,m_in,sizeof(cuda_in),cudaMemcpyHostToDevice));

				cuda_process_helper(m_devin,m_devout,64,6,numb,numt);

				cutilSafeCall(cudaMemcpy(m_out,m_devout,numb*numt*sizeof(cuda_out),cudaMemcpyDeviceToHost));

				err=cudaGetLastError();
				if(err!=cudaSuccess)
				{
					printf("CUDA error %d\n",err);
				}
			}

			int64 et=GetTimeMillis();

			printf("Finding best configuration step end (%d,%d) %"PRI64d"ms  prev best=%"PRI64d"ms\n",numb,numt,et-st,besttime);

			if((et-st)<besttime && err==cudaSuccess)
			{
				bestb=numb;
				bestt=numt;
				besttime=et-st;
			}
		}
	}

	m_numb=bestb;
	m_numt=bestt;

	AllocateResources(m_numb,m_numt);

}

const unsigned long CUDARunner::RunStep()
{
	unsigned int best=0;
	unsigned int bestg=~0;

	if(m_in==0 || m_out==0 || m_devin==0 || m_devout==0)
	{
		AllocateResources(m_numb,m_numt);
	}

	cutilSafeCall(cudaMemcpy(m_devin,m_in,sizeof(cuda_in),cudaMemcpyHostToDevice));

	cuda_process_helper(m_devin,m_devout,GetStepIterations(),GetStepBitShift(),m_numb,m_numt);

	cutilSafeCall(cudaMemcpy(m_out,m_devout,m_numb*m_numt*sizeof(cuda_out),cudaMemcpyDeviceToHost));

	for(int i=0; i<m_numb*m_numt; i++)
	{
		if(m_out[i].m_bestnonce!=0 && m_out[i].m_bestg<bestg)
		{
			best=m_out[i].m_bestnonce;
			bestg=m_out[i].m_bestg;
		}
	}

	return CryptoPP::ByteReverse(best);

}
*/
