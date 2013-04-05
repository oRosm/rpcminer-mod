"%CUDA_PATH%bin\nvcc.exe" -ptx ^
	--use-local-env --cl-version 2010 ^
	-gencode=arch=compute_20,code=\"sm_20,compute_20\" ^
	-m32 ^
	-I"%CUDA_PATH%include" ^
	-I"src\cuda" ^
	-o "bin\bitcoinminercuda.ptx" "src\cuda\bitcoinminercuda.cu"

