copy /y "src\cuda\bitcoinminercuda.cu" "bin\bitcoinminercuda.cu"

"%CUDA_PATH%bin\nvcc.exe" -ptx ^
	--use-local-env --cl-version 2010 ^
	-gencode=arch=compute_20,code=\"sm_20,compute_20\" ^
	-m32 ^
	-D_BITCOIN_MINER_CUDA_ ^
	-DNVCC ^
	-I"%CUDA_PATH%include" ^
	-I"src\cuda" ^
	-o "bin\bitcoinminercuda.ptx" ^
	"bin\bitcoinminercuda.cu"
