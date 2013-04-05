copy /y "src\cuda\bitcoinminercuda.cu" "bin\bitcoinminercuda.cu"

"%CUDA_PATH%bin\nvcc.exe" -ptx ^
	--use-local-env --cl-version 2010 ^
	-gencode=arch=compute_35,code=\"sm_35,compute_35\" -Xptxas=-v -Xopencc=-LIST:source=on ^
	-m32 ^
	-D_BITCOIN_MINER_CUDA_ ^
	-DNVCC ^
	-I"%CUDA_PATH%include" ^
	-I"src\cuda" ^
	-o "bin\bitcoinminercuda.ptx" ^
	"bin\bitcoinminercuda.cu"

