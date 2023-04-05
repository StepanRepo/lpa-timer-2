#include"../lib/raw_profile.h"
#include"../lib/session_info.h"
#include"../lib/configuration.h"
#include"../lib/massages.h"

#include <chrono>

// need libboost-dev:
// sudo dnf install boost-devel
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>


#ifdef __AVX__
#include <immintrin.h>
#else
#warning AVX is not available. Code will not compile!
#endif

#include<string>
#include<fstream>
#include<iostream>
#include <malloc.h>


using namespace std;

extern Configuration* cfg;

Raw_profile::Raw_profile(string file_name, byte32* data, double* signal, size_t len) : session_info(file_name, true)
{

	if (cfg->verbose)
		cout << "Making raw profile" << endl;

	int total_pulses = session_info.get_TOTAL_PULSES();
	int obs_window = session_info.get_OBS_WINDOW();
	int chanels = session_info.get_CHANELS();

	OBS_SIZE = total_pulses*obs_window*chanels;

	if (data != nullptr && signal != nullptr && len < (size_t) OBS_SIZE)
		throw invalid_argument (string(ERROR) + "Size of reserved memory is too small");

	bool is_data_plased = false, is_signal_placed = false;

	if (data == nullptr)
	{
		data = new byte32[OBS_SIZE];
		is_data_plased = true;
	}

	if (signal == nullptr)
	{
		signal = new double[OBS_SIZE];
		is_signal_placed = true;
	}


	read_data(file_name, data);
	decode_data(data, signal);


	mean_signal_per_chanel = vector (chanels, vector<double>(obs_window));

	split_data(signal);

	if (is_data_plased)
	{
		delete[] data;
		data = nullptr;
	}
	if(is_signal_placed)
	{
		delete[] signal;
		signal = nullptr;
	}
}

void Raw_profile::read_data(string file_name, byte32* data)
{
//	auto t1 = std::chrono::steady_clock::now();
//
//	if (cfg->verbose)
//		cout << SUB << "Reading data...";
//
//
//	ifstream obs_file (file_name, ios::in | ios::binary);
//
//	if (!obs_file)
//		throw invalid_argument (string(ERROR) + "Cann't open observational file to read data" + file_name);
//
//	// skip header of file
//	for (int i = 0; i < session_info.get_NUM_PARAMS(); i++)
//		obs_file.ignore(40, '\n');
//
//
//	obs_file.read((data[0].as_char), 4*OBS_SIZE);
//
//	obs_file.close();
//
//	if (cfg->verbose)
//		cout  << OK << endl;
//
//
	if (cfg->verbose)
		cout << SUB << "Reading data...";
	auto t1 = std::chrono::steady_clock::now();
	int HEADER_SIZE = 13*40;	



	// Create the file mapping
	boost::interprocess::file_mapping fm(file_name.c_str(), boost::interprocess::read_only);
	// Map the file in memory
	boost::interprocess::mapped_region region(fm, boost::interprocess::read_only);
	// Get the address where the file has been mapped
	
	byte32* ptr = (byte32*) region.get_address() + HEADER_SIZE/4;
	
	auto t2 = std::chrono::steady_clock::now();
	memcpy(data, ptr, OBS_SIZE*4);

	if (cfg->verbose)
		cout  << OK << endl;

	auto t3 = std::chrono::steady_clock::now();
	cout << "reading1: " << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count() << endl;
	cout << "reading2: " << std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count() << endl;
}

void Raw_profile::decode_data(byte32* data, double* signal)
{

	if (cfg->verbose)
		cout << SUB << "Decoding data...";

auto t2 = std::chrono::steady_clock::now();
#pragma omp parallel default(private) shared(data, signal) 
	{      
		double exp, spectr_t;
		double ratio = 0.2048/session_info.get_TAU();


#pragma omp for
		for (int i = 0; i < OBS_SIZE; ++i)
		{
			spectr_t = double (data[i].as_int & 0xFFFFFF);

			exp = double ( (data[i].as_int & 0x7F000000) >> 24 );
			exp -= 64.0;

			exp = double(1llu << (unsigned long long) exp);

			spectr_t = spectr_t*exp*ratio;
			spectr_t = spectr_t*1.3565771745707199e-14;

			signal[i] = spectr_t;
		}
	}

	if (cfg->verbose)
		cout << OK << endl;

	auto t3 = std::chrono::steady_clock::now();
	cout << "decoding: " << std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count() << endl;
}

void Raw_profile::split_data (double* signal)
{
	if (cfg->verbose)
		cout << SUB << "Splitting data...";

	int total_pulses = session_info.get_TOTAL_PULSES();
	int obs_window = session_info.get_OBS_WINDOW();
	int chanels = session_info.get_CHANELS();


#pragma omp parallel default(private) shared(total_pulses, obs_window, chanels, signal, mean_signal_per_chanel)  
	{      

#pragma omp for
		for (int i = 0; i < chanels; ++i)
			fill(mean_signal_per_chanel[i].begin(), mean_signal_per_chanel[i].end(), 0.0);

		int chan_and_window = chanels*obs_window;

#pragma omp for 
		for (int imp = 0; imp < total_pulses; ++imp)
		{
			for (int k = 0; k < obs_window; ++k)
			{
				for (int i = 0; i < chanels; ++i)
				{
					mean_signal_per_chanel[i][k] += signal[i + k*chanels + imp*chan_and_window];
				}
			}
		}
	}

	if (cfg->verbose)
		cout << OK << endl;

}


void Raw_profile::print_mean_channel(string file_name)
{
	ofstream out (file_name);

	if (!out)
		cout << WARNING << "Cann't print channel profiles: " << file_name << endl;
	for (int i = 0; i < 570; i++)
	{
		for (int k = 0; k < 512; k++)
			out << mean_signal_per_chanel[k][i] << " ";

		out << endl;
	}
	out.close();
}
