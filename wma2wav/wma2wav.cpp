///////////////////////////////////////////////////////////////////////////////
// wma2wav - Dump WMA files to Wave Audio
// Copyright (C) 2004-2011 LoRd_MuldeR <MuldeR2@GMX.de>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
// http://www.gnu.org/licenses/gpl-2.0.txt
///////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "WmaReader.h"
#include "WaveWriter.h"
#include "RawWriter.h"
#include "Utils.h"

#include <Objbase.h>
#include <io.h>
#include <float.h>

using namespace std;

// ----------------------------------------------------------------------------------------------------------

typedef struct
{
	bool overwriteFlag;
	bool rawOutput;
	bool silentMode;
	bool aggressiveMode;
	bool noCompensation;
	bool alternativeMode;
	wchar_t *inputFile;
	wchar_t *outputFile;
	wchar_t *title;
}
param_t;

static bool com_initialized = false;
static const char *alive = "|/-\\";

// ----------------------------------------------------------------------------------------------------------

static bool parse_cli(int argc, _TCHAR* argv[], param_t *param)
{
	param->inputFile = NULL;
	param->outputFile = NULL;
	param->overwriteFlag = false;
	param->rawOutput = false;
	param->silentMode = false;
	param->aggressiveMode = false;
	param->noCompensation = false;
	param->alternativeMode = false;
	char *temp = NULL;

	for(int i = 1; i < argc; i++)
	{
		if(!_wcsicmp(argv[i], L"-i"))
		{
			if(i < (argc - 1))
			{
				param->inputFile = argv[++i];
				continue;
			}
			else
			{
				if(temp = utf16_to_utf8(argv[i]))
				{
					fprintf(stderr, "Argument missing for command-line option:\n%s\n\n", temp);
					SAFE_DELETE_ARRAY(temp);
				}
				return false;
			}
		}
		if(!_wcsicmp(argv[i], L"-o"))
		{
			if(i < (argc - 1))
			{
				param->outputFile = argv[++i];
				continue;
			}
			else
			{
				if(temp = utf16_to_utf8(argv[i]))
				{
					fprintf(stderr, "Argument missing for command-line option:\n%s\n\n", temp);
					SAFE_DELETE_ARRAY(temp);
				}
				return false;
			}
		}
		if(!_wcsicmp(argv[i], L"-f"))
		{
			param->overwriteFlag = true;
			continue;
		}
		if(!_wcsicmp(argv[i], L"-r"))
		{
			param->rawOutput = true;
			continue;
		}
		if(!_wcsicmp(argv[i], L"-s"))
		{
			param->silentMode = true;
			continue;
		}
		if(!_wcsicmp(argv[i], L"-a"))
		{
			param->aggressiveMode = true;
			continue;
		}
		if(!_wcsicmp(argv[i], L"-n"))
		{
			param->noCompensation = true;
			continue;
		}
		if(!_wcsicmp(argv[i], L"-x"))
		{
			param->alternativeMode = true;
			continue;
		}
		
		if(temp = utf16_to_utf8(argv[i]))
		{
			fprintf(stderr, "Unknown command-line option:\n%s\n\n", temp);
			SAFE_DELETE_ARRAY(temp);
		}

		return false;
	}

	if(param->noCompensation)
	{
		if(param->aggressiveMode)
		{
			cerr << "Error: Can not use \"-a\" and \"-n\" options at the same time!\n" << endl;
			return false;
		}
		if(param->alternativeMode)
		{
			cerr << "Error: Can not use \"-x\" and \"-n\" options at the same time!\n" << endl;
			return false;
		}
	}
	
	if(!((param->inputFile) && (param->outputFile)))
	{
		cerr << "Error: Input and/or output file not specified!\n" << endl;
		return false;
	}

	if((!_wcsicmp(param->outputFile, L"-")) && (!(param->rawOutput)))
	{
		cerr << "Output to STDOUT requires \"raw\" mode -> switching to \"raw\" mode!\n" << endl;
		param->rawOutput = true;
	}

	return true;
}

// ----------------------------------------------------------------------------------------------------------

static int wma2wav(int argc, _TCHAR* argv[])
{
	cerr << "wma2wav - Dump WMA/WMV files to Wave Audio [" __DATE__ "]" << endl;
	cerr << "Copyright (c) 2011 LoRd_MuldeR <mulder2@gmx.de>. Some rights reserved." << endl;
	cerr << "Released under the terms of the GNU General Public License.\n" << endl;

	CAbstractSink *sink = NULL;
	CWmaReader *wmaReader = NULL;
	param_t param;
	SecureZeroMemory(&param, sizeof(param_t));
	WAVEFORMATEX format;
	SecureZeroMemory(&format, sizeof(WAVEFORMATEX));
	double duration = -1.0;
	BYTE *buffer = NULL;
	size_t bufferLen = 0;
	size_t sampleLen = 0;
	double currentTime = 0.0;
	short indicator = 0;
	short aliveIndex = 0;
	size_t stats[2] = {0, 0};
	char *temp = NULL;
	
	if(!parse_cli(argc, argv, &param))
	{
		cerr << "Usage:" << endl;
		cerr << "  wma2wav.exe [options] -i <input> -o <output>\n" << endl;
		cerr << "Options:" << endl;
		cerr << "  -i <input>   Select input ASF (WMA/WMV) file to read from" << endl;
		cerr << "  -o <output>  Select output Wave file to write to, specify \"-\" for STDOUT" << endl;
		cerr << "  -f           Force overwrite of output file (if already exists)" << endl;
		cerr << "  -r           Output \"raw\" PCM data to file instead of Wave/RIFF file" << endl;
		cerr << "  -s           Silent mode, do not display progress indicator" << endl;
		cerr << "  -x           Use the \"alternative\" timestamp calculation mode" << endl;
		cerr << "  -a           Enable \"aggressive\" sync correction mode (not recommended)" << endl;
		cerr << "  -n           No sync correction (can not use with '-a' or '-x')\n" << endl;
		cerr << "Example:" << endl;
		cerr << "  wma2wav.exe \"c:\\my music\\input.wma\" \"c:\\temp\\output.wav\"\n" << endl;
		return 1;
	}

	const double maxGapSize = (param.noCompensation) ? numeric_limits<double>::infinity() : ((param.aggressiveMode) ? 0.00000001 : 0.00100001);

	if(CoInitializeEx(NULL, COINIT_MULTITHREADED) != S_OK)
	{
		cerr << "Fatal Error: COM initialization has failed unexpectedly!" << endl;
		_exit(-1);
	}
	
	com_initialized = true;

	if(temp = utf16_to_utf8(param.inputFile))
	{
		fprintf(stderr, "Input file: %s\n", temp);
		SAFE_DELETE_ARRAY(temp);
	}
	if(_wcsicmp(param.outputFile, L"-"))
	{
		if(temp = utf16_to_utf8(param.outputFile))
		{
			fprintf(stderr, "Output file: %s\n\n", temp);
			SAFE_DELETE_ARRAY(temp);
		}
	}
	else
	{
		fprintf(stderr, "Output file: <STDOUT>\n\n");
	}
	
	if(_waccess(param.inputFile, 4))
	{
		cerr << "Error: Input file could not be found or access denied!" << endl;
		return 2;
	}
	if(_wcsicmp(param.outputFile, L"-") && _wcsicmp(param.outputFile, L"NUL") && (!(param.overwriteFlag)))
	{
		if(!_waccess(param.outputFile, 4))
		{
			cerr << "Error: Output file already exists, will NOT overwrite!" << endl;
			return 3;
		}
	}

	wmaReader = new CWmaReader();
	cerr << "Opening input file... " << flush;

	if(wmaReader->isProtected(param.inputFile))
	{
		cerr << "Failed\n\nSorry, DRM infected ASF (WMA/WMV) files can not be processed!" << endl;
		SAFE_DELETE(wmaReader);
		return 4;
	}
	if(!wmaReader->open(param.inputFile))
	{
		cerr << "Failed\n\nMake sure that the input file is a valid ASF (WMA/WMV) file!" << endl;
		SAFE_DELETE(wmaReader);
		return 4;
	}
	
	cerr << "OK\nAnalyzing input file... " << flush;

	if(!wmaReader->analyze())
	{
		cerr << "Failed\n\nThis usually indicates that the ASF file contains no suitable audio stream." << endl;
		SAFE_DELETE(wmaReader);
		return 5;
	}
		
	if(!wmaReader->getFormat(&format))
	{
		cerr << "Internal error, aborting! (0x01)" << endl;
		_exit(-1);
	}

	cerr << "OK\n\n[Audio Properties]" << endl;
	cerr << "wFormatTag: " << hex << format.wFormatTag << dec << endl;
	cerr << "nChannels: " << format.nChannels << endl;
	cerr << "nSamplesPerSec: " << format.nSamplesPerSec << endl;
	cerr << "wBitsPerSample: " << format.wBitsPerSample << endl;

	if((duration = wmaReader->getDuration()) > 0.0)
	{
		double duration_minutes, duration_seconds;
		seconds_to_minutes(duration, &duration_minutes, &duration_seconds);
		fprintf(stderr, "fDuration: %.0f:%04.1f\n", duration_minutes, duration_seconds);
	}
	
	if(param.title = wmaReader->getTitle())
	{
		if(temp = utf16_to_utf8(param.title))
		{
			fprintf(stderr, "sTitle: %s\n", temp);
			SAFE_DELETE_ARRAY(temp);
		}
		SAFE_DELETE_ARRAY(param.title);
	}

	if((bufferLen = wmaReader->getSampleSize()) < 1)
	{
		cerr << "\nFailed to detect maximum sample size!" << endl;
		SAFE_DELETE(wmaReader);
		return 6;
	}
	
	cerr << "nMaxSampleSize: " << bufferLen << endl;
	cerr << "\nOpening output file... " << flush;

	sink = (!(param.rawOutput)) ? dynamic_cast<CAbstractSink*>(new CWaveWriter()) : dynamic_cast<CAbstractSink*>(new CRawWriter());

	if(!sink->open(param.outputFile, &format))
	{
		cerr << "Failed" << endl;
		SAFE_DELETE(wmaReader);
		return 7;
	}

	cerr << "OK\n" << endl;

	if(param.silentMode)
	{
		cerr << "Dumping audio samples to file, please be patient..." << flush;
	}

	bufferLen = ((bufferLen / 4096) + 1) * 4096;
	buffer = new BYTE[bufferLen];
	SecureZeroMemory(buffer, bufferLen);

	while(true)
	{
		if((!indicator) && (!(param.silentMode)))
		{
			if(duration > 0.0)
			{
				double completed = min((currentTime / duration) * 100.0, 100.0);
				double currentTime_minutes, currentTime_seconds, duration_minutes, duration_seconds;
				seconds_to_minutes(currentTime, &currentTime_minutes, &currentTime_seconds);
				seconds_to_minutes(duration, &duration_minutes, &duration_seconds);
				fprintf(stderr, "\r[%3.1f%%] %.0f:%04.1f of %.0f:%04.1f completed, please wait... %c", completed, currentTime_minutes, currentTime_seconds, duration_minutes, duration_seconds, alive[aliveIndex]);
			}
			else
			{
				double currentTime_minutes, currentTime_seconds;
				seconds_to_minutes(currentTime, &currentTime_minutes, &currentTime_seconds);
				fprintf(stderr, "\r%.0f:%04.1f seconds completed so far... %c", currentTime_minutes, currentTime_seconds, alive[aliveIndex]);
			}
			
			aliveIndex = (aliveIndex + 1) % 4;
			fflush(stderr);
		}
		
		indicator = (indicator + 1) % 25;
		
		double sampleDuration = -1.0;
		double sampleTimestamp = -1.0;
		size_t skipBytes = 0;

		if(!wmaReader->getNextSample(buffer, &sampleLen, &sampleTimestamp, &sampleDuration))
		{
			cerr << "\n\nFailed to read sample from input file!" << endl;
			SAFE_DELETE(sink);
			SAFE_DELETE(wmaReader);
			SAFE_DELETE_ARRAY(buffer);
			return 8;
		}
		
		if(!(sampleLen > 0))
		{
			if(!(param.silentMode))
			{
				double currentTime_minutes, currentTime_seconds;
				seconds_to_minutes(currentTime, &currentTime_minutes, &currentTime_seconds);
				fprintf(stderr, "\r[%3.1f%%] %.0f:%04.1f of %.0f:%04.1f completed, please wait... %c", 100.0, currentTime_minutes, currentTime_seconds, currentTime_minutes, currentTime_seconds, 0x20);
				fflush(stderr);
			}
			break;
		}

		if((sampleLen % ((format.wBitsPerSample / 8) * format.nChannels)) > 0)
		{
			fprintf(stderr, "\rInconsistent sample length: %I64u is not a multiple of %I64u.\n", static_cast<unsigned __int64>(sampleLen), static_cast<unsigned __int64>((format.wBitsPerSample / 8) * format.nChannels));
		}
		
		//if(abs(sampleDuration - bytes_to_time(sampleLen, &format)) > maxGapSize)
		//{
		//	fprintf(stderr, "\rInconsistent duration: Got samples for %10.8f seconds, but duration is %10.8f.\n", bytes_to_time(sampleLen, &format), sampleDuration);
		//}

		if((sampleTimestamp >= 0.0) && (abs(sampleTimestamp - currentTime) > maxGapSize))
		{
			if(!(param.silentMode))
			{
				fprintf(stderr, "\rInconsistent timestamps: Expected %10.8f next, but got %10.8f.\n", currentTime, sampleTimestamp);
			}

			if(sampleTimestamp > currentTime)
			{
				size_t paddingBytes = time_to_bytes((sampleTimestamp - currentTime), &format);
				
				if(paddingBytes > 0)
				{
					BYTE zeroBuffer[1024];
					SecureZeroMemory(zeroBuffer, 1024);

					if(!(param.silentMode))
					{
						fprintf(stderr, "There is a \"gap\" of %10.8f seconds, padding %I64u zero bytes!\n", (sampleTimestamp - currentTime), static_cast<unsigned __int64>(paddingBytes));
					}

					currentTime += bytes_to_time(paddingBytes, &format);
					stats[0] += paddingBytes;

					while(paddingBytes > 0)
					{
						size_t currentSize = min(paddingBytes, 1024);
						if(!sink->write(currentSize, zeroBuffer))
						{
							cerr << "\n\nFailed to write sample to output file!" << endl;
							SAFE_DELETE(sink);
							SAFE_DELETE(wmaReader);
							SAFE_DELETE_ARRAY(buffer);
							return 9;
						}
						paddingBytes -= currentSize;
					}
				}
			}
			else
			{
				size_t offsetBytes = time_to_bytes((currentTime - sampleTimestamp), &format);
				
				if(offsetBytes > 0)
				{
					skipBytes = min(offsetBytes, sampleLen);
					stats[1] += skipBytes;

					if(!(param.silentMode))
					{
						fprintf(stderr, "The samples \"overlap\" for %10.8f seconds, skipping %I64u bytes!\n", (currentTime - sampleTimestamp), static_cast<unsigned __int64>(skipBytes));
					}

					currentTime -= bytes_to_time(skipBytes, &format);
				}
			}
		}

		if(!sink->write((sampleLen - skipBytes), (buffer + skipBytes)))
		{
			cerr << "\n\nFailed to write sample to output file!" << endl;
			SAFE_DELETE(sink);
			SAFE_DELETE(wmaReader);
			SAFE_DELETE_ARRAY(buffer);
			return 9;
		}

		if((sampleDuration >= 0.0) && (!(param.alternativeMode)))
		{
			if(sampleTimestamp >= 0.0)
			{
				currentTime = sampleTimestamp + sampleDuration;
			}
			else 
			{
				currentTime += bytes_to_time(sampleLen, &format);
			}
		}
		else
		{
			currentTime += bytes_to_time(sampleLen, &format);
		}
	}

	if(!sink->close())
	{
		cerr << "\n\nError: Failed to properly close the output file!" << endl;
		SAFE_DELETE(wmaReader);
		SAFE_DELETE(sink);
		SAFE_DELETE_ARRAY(buffer);
		return 10;
	}

	
	if((stats[0] > 0) || (stats[1] > 0))
	{
		cerr << "\nWarning: Sync correction inserted " << stats[0] << " zero bytes, skipped " << stats[1] << " bytes." << flush;
	}

	cerr << "\n\nAll done." << endl;

	SAFE_DELETE(wmaReader);
	SAFE_DELETE(sink);
	SAFE_DELETE_ARRAY(buffer);

	return 0;
}

// ----------------------------------------------------------------------------------------------------------

static int wmain2(int argc, _TCHAR* argv[])
{
	try
	{
		int result = wma2wav(argc, argv);
		SAFE_COM_UNINIT(com_initialized);
		return result;
	}
	catch(std::bad_alloc err)
	{
		fprintf(stderr, "\nMemory allocation has failed, application will exit!\n");
		return -1;
	}
	catch(char *err)
	{
		fprintf(stderr, "\n%s\n", err);
		return -1;
	}
	catch(...)
	{
		fprintf(stderr, "\nUnhandeled exception error, application will exit!\n");
		return -1;
	}
}

int wmain(int argc, _TCHAR* argv[])
{
	__try
	{
		repair_standard_streams();
		int result = wmain2(argc, argv);
		restore_previous_codepage();
		return result;
	}
	__except(1)
	{
		fprintf(stderr, "\nUnhandeled system exception, application will exit!\n");
		TerminateProcess(GetCurrentProcess(), -1);
		return -1;
	}
}
