/**
 * @file mLSH.cpp
 *
 * @author 黄梓生
 * 
 * 实现局部敏感哈希算法，用于相似文档的查找
 *
 * 2019年12月26日
 */

#include <iostream>
#include <vector>
#include <deque>
#include <sstream>
#include <string>
#include <functional>
#include <fstream>
#include <set>
#include <map>
#include <algorithm>
#include <ctime>
#include <io.h>

using namespace std;

const int K = 5;		//每五个单词作为一个字符串
const int N = 100;		//签名矩阵的行大小为100
const double thre = 0.8;//相似度的阈值设为0.8，找出相似度超过阈值的所有文档对
vector<string> files;	//存储数据集文件夹里所有文档的名称，方便最后输出结果
set<char> uselessChar = {',', '.', ';', ':', '"', '!', '?', '/', '\\', '\n', '\r'}; //标点符号不纳入相似度的度量
vector<vector<string> > documents; //二维的向量，用于存储每一篇文档的所有单词
vector<uint32_t*> cm;  //特征值矩阵，由于该矩阵十分稀疏，并且每个元素非0即1，因此这里使用位向量来表示，节省空间
map<string, vector<int> > cmHelper; //用于帮助构建特征值矩阵，存储指定的字符串在哪些文档中出现过
vector<vector<int> >sm; //签名矩阵
map<pair<int, int>, double> candidates; //存储候选的文档对以及它们的相似度

//获取指定文件夹下所有文档的名称，并存储起来
void GetFiles(string floder, vector<string>& files)
{
	//文件句柄  
	long fh = 0;
	//文件信息  
	struct _finddata_t fileinfo;
	string temp;
	if ((fh = _findfirst(temp.assign(floder).append("\\*").c_str(), &fileinfo)) != -1)
	{
		do
		{
			//如果是目录,迭代之  
			//如果不是,加入列表  
			if ((fileinfo.attrib &  _A_SUBDIR))
			{
				if (strcmp(fileinfo.name, ".") != 0 && strcmp(fileinfo.name, "..") != 0)
					GetFiles(temp.assign(floder).append("\\").append(fileinfo.name), files);
			}
			else
			{
				files.push_back(temp.assign(floder).append("\\").append(fileinfo.name));
			}
		} while (_findnext(fh, &fileinfo) == 0);
		_findclose(fh);
	}
	cout << files.size() << " Documents" << endl; //输出文档的总数量
}

//读取指定文档内容（除去标点符号）
void ReadDocument(const char *file)
{
	vector<string> doc;
	ifstream fileReader;
	fileReader.open(file); //打开文件
	string temp;
	while (!fileReader.eof()) //遍历文件内容
	{
		string str;
		fileReader >> temp;	
		for (int i = 0; i < temp.size(); ++i)
		{
			char c = temp[i];
			if (uselessChar.find(c) == uselessChar.end())	//如果当前字符不属于标点符号
			{
				str += c;
			}
		}
		doc.push_back(str); 
	}
	documents.push_back(doc);
	fileReader.close();
}

//读取用户在命令行窗口输入的目标文档以及比对数据集文件夹下的所有文档
void ReadAllDocuments(int argc, char *argv[])
{
	cout << "ReadAllDocuments......" << endl;
	ReadDocument(argv[1]);
	for (int i = 0; i < files.size(); ++i)
	{
		const char *temp = files[i].c_str();
		ReadDocument(temp);
	}
}

//将k个连续的单词作为一个字符串
string Kwords(deque<string>& de)
{
	string ret;
	for (int i = 0; i < de.size(); ++i)
	{
		ret += de[i];
		if (i != de.size()-1)
			ret += " ";
	}
	return ret;
}

//将每篇文档中的连续k个单词作为一个字符串，用cmHelper(一个对应类型的map)存储每个字符串
//在哪些文档中出现过，方便后续计算特征值矩阵
void GenerateKShingles(int k)
{
	cout << "GenerateKShingles...... " << endl;
	int docNum = documents.size();	 //所有文档的数目
	for (int i = 0; i < docNum; ++i) //遍历每一篇文档
	{
		int curDocSize = documents[i].size(); //当前文档的单词数目
		deque<string> Kstrings;				  //用于存储连续的k个单词
		for (int j = 0; j < k; ++j)
		{
			Kstrings.push_back(documents[i][j]);
		}
		//k个连续单词作为一个字符串，然后用cmHelper存储出现过该字符串的文档的编号
		cmHelper[Kwords(Kstrings)].push_back(i); 
		
		//对当前文档的剩余单词，继续做上述处理
		//移除当前k个单词的第一个，往后追加未遍历的单词
		for (int x = k; x < curDocSize; ++x)
		{
			Kstrings.pop_front();
			Kstrings.push_back(documents[i][x]);
			cmHelper[Kwords(Kstrings)].push_back(i);
		}
	}
}

//生成特征值矩阵
void GenerateCM()
{
	cout << "GenerateCM......" << endl;
	int docNum = documents.size(); //所有文档的数目

	//cm是一个特征值矩阵，行表示字符串，列表示文档编号
	//如果当前字符串i在文档j中出现过，则cm[i][j]为1，否则为0
	//uint32_t表示32位的无符号整数，使用uint32_t数组来表示所有文档编号，从左往右数
	//因此需要docNum/32 + 1个uint32_t来表示
	int uintNum = docNum/32 + 1;
	//遍历每一个字符串，根据出现过该字符串的文档编号，来生成特征值矩阵
	for (auto item : cmHelper)
	{
		uint32_t* temp = new uint32_t[uintNum]();
		//将出现过该字符串的文档编号在cm矩阵中的值设为1
		for (int i : item.second)
		{
			int index = i / 32;
			int off = i % 32;		
			//将temp[index]上从左往右数第off位设为1
			temp[index] = temp[index] | (1 << (31 - off));
		}
		cm.push_back(temp);
	}
}

//生成随机排列，用于计算签名矩阵
//vec里存放的是特征值矩阵中的行编号
//该函数对所有行编号进行一次随机排列，并存储
void GenerateRandomPermutation(vector<int>& vec)	
{
	int n = vec.size(); //总共有多少行
	for (int i = 0; i < n; ++i)
	{
		int r = rand() % n;
		int temp = vec[i];
		vec[i] = vec[r];
		vec[r] = temp;
	}
}

//生成签名矩阵
void GenerateSM()
{
	cout << "GenerateSM......" << endl;
	int docNum = documents.size();	   //文档的总数目
	int strNum = cmHelper.size();     //所有文档里用于比对的字符串的个数
	vector<int> index(strNum);		   //index用于存储cm矩阵的行编号
	for (int i = 0; i < strNum; ++i)   //初始化为递增排序
		index[i] = i;

	//sm矩阵的行大小为100， 列大小为文档的总数目
	sm = vector<vector<int> >(N, vector<int>(docNum));
	
	for (int i = 0; i < N; ++i)
	{
		vector<int> temp(docNum);
		GenerateRandomPermutation(index);   //产生一次随机排列

		//按照index里指定的访问顺序对cm进行访问
		//找出每篇文档按此顺序访问时，第一个值为1的元素所在的行号
		for (int j = 0; j < docNum; ++j)	
		{
			for (int x = 0; x < strNum; ++x)
			{
				int curIndex = index[x]; //当前访问的cm的行号
				int t = j >> 5;			 //右移动5位表示除以32，结果也就是当前文档编号是在第几个uint32_t
				int off = j % 32;		 //结果表示当前文档编号是在当前uint32_t的第off位（从左往右数）
				uint32_t num = cm[curIndex][t];
				//判断第off位是否为1，也就是该字符串是否出现在当前文档中
				//如果是，则表示按照index的访问顺序，已找到当前文档出现的第一个1的行号，退出当前循环
				num &= (1 << (31 - off));
				if (num != 0)
				{
					temp[j] = index[x];
					break;
				}
			}
		}
		sm[i] = temp;
	}
}

//在sm中找到与目标文档最相似的文档
void FindMostSimilar()
{
	cout << "FindMostSimilar......" << endl;
	double ret = 0;
	int r = sm.size();
	int docNum = sm[0].size();
	int index = 1; //index用于记录最相似文档的编号

	//目标文档位于sm矩阵的第一列，也就是列下标为0，所以从列下标从1开始比较
	for (int i = 1; i < docNum; ++i)
	{
		double temp = 0;
		for (int j = 0; j < r; ++j)
		{
			if (sm[j][i] == sm[j][0])
				++temp;
		}
		if (temp > ret)
		{
			ret = temp;
			index = i;
		}
	}
	ret = ret / r;
	cout << "输入的文档与" << files[index-1] << "最相似，相似度为: " << ret << endl;
}

//计算指定文档对的jaccard相似度
double ComputeSimilarity(int doc1, int doc2) 
{
	double ret = 0;
	for (int i = 0; i < N; i++) 
	{
		if (sm[i][doc1] == sm[i][doc2]) 
			ret++;
	}
	ret = ret / N;
	return ret;
}

//找出通过哈希函数后映射值相同的文档对，作为候选文档对
void FindCandidates() 
{
	int b = 20, r = 5;			//sm的行大小为100，分成20组，每组5行
	int docNum = sm[0].size();	//文档的总数
	hash<string> strHashFunc;	//使用c++提供的对于string的hash函数就行
	//用20组桶来存储元素通过哈希函数后映射的值
	vector<vector<int> > buckets(b, vector<int>(docNum));  

	//下面的循环，计算每一组里的元素通过哈希函数后映射的值
	for (int band = 0; band < b; band++) 
	{
		for (int c = 1; c < docNum; c++) 
		{
			char temp[5];
			for (int row = 0; row < r; row++) 
			{
				temp[row] = sm[band*r + row][c];
			}
			string str = temp;
			//存储映射的值
			buckets[band][c] = strHashFunc(str);
		}
	}

	//找出候选的文档对
	for (int band = 0; band < b; band++) 
	{
		for (int i = 0; i < docNum - 1; i++) 
		{
			for (int j = i + 1; j < docNum; j++) 
			{
				//如果当前两篇文档都映射到同一个桶里，则把它们作为候选的文档对
				if (buckets[band][i] == buckets[band][j]) 
				{
					pair<int, int> temp;
					temp.first = i;
					temp.second = j;
					candidates[temp] = 0;
				}
			}
		}
	}

	//计算候选文档对的相似度，并存储起来
	auto it = candidates.begin();
	while (it != candidates.end()) 
	{
		it->second = ComputeSimilarity(it->first.first, it->first.second);
		it++;
	}
}

//输出相似文档对，这里的阈值设为0.8 
void PrintSimilarDocuments() 
{
	int similarPair = 0; //相似度大于阈值的文档对数目
	auto it = candidates.begin();

	//遍历所有的候选文档对
	while (it != candidates.end()) 
	{
		//如果当前文档对的相似度大于阈值，则输出
		if (it->second >= thre) 
		{
			cout << "文档" << files[(it->first.first)-1] 
				 << " 和文档" << files[(it->first.second)-1] 
				 << " 达到" << thre << "以上的相似度" << endl;

			similarPair++;
		}
		it++;
	}
	if (similarPair == 0)
	{
		cout << "没有相似度大于" << thre << "的文档对" << endl;
	}
}

//释放cm所申请的内存空间
void ReleaseResource()
{
	for (auto item : cm)
	{
		delete[] item;
	}
}

int main(int argc, char *argv[])
{
	if (argc != 3)
	{
		cout << "Usages: lsh xxx flodername" << endl;
		return -1;
	}

	srand(time(NULL));

	clock_t startTime, endTime;  //用于计算程序运行时间
	startTime = clock();		 //开始时间
	
	string floder = argv[2];	 //比对数据集的文件夹名称
	GetFiles(floder, files);	 //获取文件里里所有文档的名称
	ReadAllDocuments(argc, argv);//读取所有文档
	GenerateKShingles(K);		 //每篇文档里K个单词作为一个字符串
	GenerateCM();				 //生成特征值矩阵
	GenerateSM();		         //生成签名矩阵
	FindMostSimilar();			 //找到与目标文档最相似的文档
	FindCandidates();			 //找出所有相似的文档对
	PrintSimilarDocuments();	 //输出相似度大于0.8的文档对
	ReleaseResource();			 //释放申请的内存资源

	endTime = clock();			 //结束时间

	cout << "耗时" << (double)((endTime - startTime) / CLOCKS_PER_SEC) << " 秒" << endl;
	return 0;
}
