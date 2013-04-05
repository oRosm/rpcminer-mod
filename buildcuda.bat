call "%1"bin\vcvars32.bat

"%CUDA_PATH%bin\nvcc.exe" -ptx ^
	--use-local-env --cl-version 2010 ^
	-gencode=arch=compute_35,code=\"sm_35,compute_35\" -Xptxas=-v -Xopencc=-LIST:source=on ^
	-m32 ^
	-I"%CUDA_PATH%include" ^
	-I"src\cuda" ^
	-o "bin\bitcoinminercuda.ptx" "src\cuda\bitcoinminercuda.cu"

