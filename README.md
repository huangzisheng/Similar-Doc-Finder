# Similar-Doc-Finder
利用局部敏感哈希算法实现的一个相似文档查找程序

支持在数据集里找到与目标文档最相似的文档，同时找出数据集里所有相似度大于阈值（默认0.8）的文档对

目前只能针对英文文档，且只支持普通的文件格式

开发环境: Visual Studio 2017

测试的数据集：20 newsgroups里的rec.sport.baseball，共1000个文档

命令行窗口使用方法：FindSimilarDoc yourFile dataSetFloder，也就是编译的程序名+目标文档+数据集所在的文件夹

程序结果截图: ![image](https://github.com/huangzisheng/Similar-Doc-Finder/blob/master/output.jpg)

