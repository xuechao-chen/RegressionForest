#include "TrainingSet.h"
#include <fstream>
#include <string>
#include <boost/algorithm/string.hpp>
#include "common/CommonInterface.h"
#include "common/ConfigParser.h"
#include "common/HiveCommonMicro.h"
#include "common/OOInterface.h"
#include "common/ProductFactoryData.h"
#include "TrainingSetConfig.h"
#include "TrainingSetCommon.h"
#include "BaseBootstrapSelector.h"
#include "BaseInstanceWeightMethod.h"

using namespace hiveRegressionForest;

CTrainingSet::CTrainingSet()
{
}

CTrainingSet::~CTrainingSet()
{
	if(m_pResponseSet) _SAFE_DELETE_ARRAY(m_pResponseSet);
}

//****************************************************************************************************
//FUNCTION:
bool CTrainingSet::loadTrainingSet(const std::string& vConfig, bool vHeader)
{
	__initTrainingSetConfig(vConfig);
	_ASSERTE(m_pResponseSet && !m_FeatureSet.empty());
	
	bool IsBinaryFile = CTrainingSetConfig::getInstance()->getAttribute<bool>(hiveRegressionForest::KEY_WORDS::IS_BINARY_TRAINGSETFILE);
	std::string TrainingSetFile = CTrainingSetConfig::getInstance()->getAttribute<std::string>(hiveRegressionForest::KEY_WORDS::TRAININGSET_PATH);
	
	bool IsLoadDataSuccess = IsBinaryFile ? __loadSetFromBinaryFile(TrainingSetFile) : __loadSetFromCSVFile(TrainingSetFile, vHeader);
	
	return IsLoadDataSuccess;
}

//****************************************************************************************************
//FUNCTION:
void CTrainingSet::recombineBootstrapDataset(const std::vector<int>& vBootstrapIndexSet, const std::pair<int, int>& vBootstrapIndexRange, std::pair<std::vector<std::vector<float>>, std::vector<float>>& voFeatureResponseSet)
{
	_ASSERTE(!vBootstrapIndexSet.empty());

	int Range = vBootstrapIndexRange.second - vBootstrapIndexRange.first;
	_ASSERTE(Range > 0);	
	voFeatureResponseSet.first.resize(Range);
	voFeatureResponseSet.second.resize(Range * m_NumResponse);

	int ResponseIndex = 0;
	while (ResponseIndex < m_NumResponse)
	{
		int n = ResponseIndex * Range;
		for (auto i = vBootstrapIndexRange.first; i < vBootstrapIndexRange.second; ++i)
		{
			if (ResponseIndex == 0) voFeatureResponseSet.first[n] = getFeatureInstanceAt(vBootstrapIndexSet[i]);
			voFeatureResponseSet.second[n] = getResponseValueAt(vBootstrapIndexSet[i], ResponseIndex);
			++n;
		}
		++ResponseIndex;
	}
}

//****************************************************************************************************
//FUNCTION:
void CTrainingSet::dumpFeatureValueSetAt(const std::vector<int>& vInstanceIndexSet, unsigned int vFeatureIndex, std::vector<float>& voValueSet)
{
	_ASSERTE(!vInstanceIndexSet.empty());

	voValueSet.resize(vInstanceIndexSet.size());
	for (unsigned int i = 0; i < vInstanceIndexSet.size(); ++i)
		voValueSet[i] = getFeatureValueAt(vInstanceIndexSet[i], vFeatureIndex);
}

//****************************************************************************************************
//FUNCTION:
void CTrainingSet::__initTrainingSetConfig(const std::string& vConfig)
{
	_ASSERTE(!vConfig.empty());

	bool IsConfigParsed = hiveConfig::hiveParseConfig(vConfig, hiveConfig::EConfigType::XML, CTrainingSetConfig::getInstance());
	_ASSERTE(IsConfigParsed);

	const CTrainingSetConfig* pTrainingSetConfig = CTrainingSetConfig::getInstance();
	unsigned int NumInstance = pTrainingSetConfig->getAttribute<int>(hiveRegressionForest::KEY_WORDS::NUM_OF_INSTANCE);
	unsigned int NumFeature  = pTrainingSetConfig->getAttribute<int>(hiveRegressionForest::KEY_WORDS::NUM_OF_FEATURE);
	m_NumResponse = pTrainingSetConfig->getAttribute<int>(hiveRegressionForest::KEY_WORDS::NUM_OF_RESPONSE);

	m_FeatureSet.resize(NumInstance);
	for (auto& Itr : m_FeatureSet) Itr.resize(NumFeature);

	m_pResponseSet = new float[NumInstance * m_NumResponse];
}

//*********************************************************************
//FUNCTION:
bool CTrainingSet::__loadSetFromCSVFile(const std::string& vCSVFile, bool vHeader)
{
	_ASSERTE(!vCSVFile.empty());

	std::ifstream DataFile(vCSVFile);
	if (DataFile.is_open())
	{
		std::string Line;

		if (vHeader)
		{
			getline(DataFile, Line);
			std::cout << "Ignore header of [" + vCSVFile + "] by default.\n";
		}

		std::vector<std::string> InstanceString;
		for (int i = 0; i < m_FeatureSet.size(); ++i)
		{
			getline(DataFile, Line);
			InstanceString.clear();
			boost::split(InstanceString, Line, boost::is_any_of(", "));
			_ASSERTE(!InstanceString.empty());

			if (!m_FeatureSet.empty() && (InstanceString.size() != (m_FeatureSet[0].size() + m_NumResponse)))
			{
				DataFile.close();
				m_FeatureSet.clear();
				_SAFE_DELETE_ARRAY(m_pResponseSet);

				return false;
			}

			int InstanceIndex = 0;
			m_FeatureSet[i].resize(m_FeatureSet[0].size());
			for (InstanceIndex = 0; InstanceIndex < m_FeatureSet[0].size(); ++InstanceIndex)
				m_FeatureSet[i][InstanceIndex] = std::stof(InstanceString[InstanceIndex].c_str());

			for (int m = 0; m < m_NumResponse; ++m)
				m_pResponseSet[i*m_NumResponse + m] = std::stof(InstanceString[InstanceIndex + m]);
		}

		DataFile.close();

		return true;
	}
	else
	{
		std::cout << "Failed to open file [" + vCSVFile + "], check file path.\n";

		return false;
	}
}

//*********************************************************************
//FUNCTION:
bool CTrainingSet::__loadSetFromBinaryFile(const std::string& vBinaryFile)
{
	std::ifstream BinaryDataFile(vBinaryFile, std::ios::binary | std::ios::in);

	unsigned int InstanceSize = m_FeatureSet[0].size() + m_NumResponse;
	float *TempData = new float[m_FeatureSet.size()*InstanceSize];
	_ASSERTE(TempData);

	if (BinaryDataFile.is_open())
	{
		BinaryDataFile.read((char*)TempData, sizeof(float)*m_FeatureSet.size()*InstanceSize);
		for (int i = 0; i < m_FeatureSet.size(); ++i)
		{
			for (int k = 0; k < InstanceSize; k++)
			{
				if (k < m_FeatureSet[0].size())
					m_FeatureSet[i][k] = TempData[i*InstanceSize + k];
				else
					m_pResponseSet[i*m_NumResponse+(k - m_FeatureSet[0].size())] = TempData[i*InstanceSize + k];
			}
		}

		_SAFE_DELETE_ARRAY(TempData);
		BinaryDataFile.close();

		return true;
	}
	else
	{
		std::cout << "Failed to open file [" + vBinaryFile + "], check file path.\n";

		return false;
	}
}
