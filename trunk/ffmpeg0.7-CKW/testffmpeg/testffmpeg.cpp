// testffmpeg.cpp : �������̨Ӧ�ó������ڵ㡣
//

#include "stdafx.h"
#include "testffmpeglib.h"

static int runffmpegTestFunction(void);

int _tmain(int argc, _TCHAR* argv[])
{
	runffmpegTestFunction();
	return 0;
}


static int runffmpegTestFunction(void)
{
	ffmpegTest_libavutil();
	return 0;
}