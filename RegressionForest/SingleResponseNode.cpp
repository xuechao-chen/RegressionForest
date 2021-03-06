#include "SingleResponseNode.h"
#include "RegressionForestCommon.h"
#include "math/AverageOutputRegression.h"
#include "common/HiveCommonMicro.h"
#include "common/ProductFactory.h"

using namespace hiveRegressionForest;

hiveOO::CProductFactory<CSingleResponseNode> theCreator(KEY_WORDS::SINGLE_RESPONSE_NODE);

CSingleResponseNode::CSingleResponseNode()
{
}

CSingleResponseNode::~CSingleResponseNode()
{
}

//****************************************************************************************************
//FUNCTION:
void CSingleResponseNode::createAsLeafNodeV(const std::pair<std::vector<std::vector<float>>, std::vector<float>>& vBootstrapDataset)
{
	_ASSERTE(CTrainingSet::getInstance()->getNumOfResponse() == 1);

	m_IsLeafNode = true;

	std::string LeafNodeModelSig = CRegressionForestConfig::getInstance()->getAttribute<std::string>(KEY_WORDS::LEAF_NODE_MODEL_SIGNATURE);
	if (LeafNodeModelSig.empty()) LeafNodeModelSig = KEY_WORDS::REGRESSION_MODEL_AVERAGE;

#pragma omp critical
	{
		m_NodeVariance = _calculateVariance(vBootstrapDataset.second);

		m_pRegressionModel = hiveRegressionAnalysis::hiveTrainRegressionModel(vBootstrapDataset.first, vBootstrapDataset.second, LeafNodeModelSig);
	}

	setBestSplitFeatureAndGap(0, FLT_MAX);
}

//****************************************************************************************************
//FUNCTION:
float CSingleResponseNode::predictV(const std::vector<float>& vFeatureInstance, unsigned int vResponseIndex) const
{
	_ASSERTE(!vFeatureInstance.empty());

	return hiveRegressionAnalysis::hiveExecuteRegression(m_pRegressionModel, vFeatureInstance);
}

//****************************************************************************************************
//FUNCTION:
float CSingleResponseNode::_getNodeVarianceV(unsigned int vResponseIndex /*= 0*/) const
{
	return m_NodeVariance;
}