#ifndef smoothDeformer_H
#define smoothDeformer_H

#include <thread>

#include <maya/MThreadPool.h>
#include <maya/MPxDeformerNode.h>
#include <maya/MItGeometry.h>
#include <maya/MItMeshVertex.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnMesh.h>
#include <maya/MPointArray.h>
#include <maya/MGlobal.h>
#include <maya/MTypes.h>


struct TaskData{
    MPointArray points;
    MPointArray newPoints;
    MFloatVectorArray normals;
    float envelope;
    int iterations;
    float maintainValue;
    short smoothBorders;
    float lambda;
    MObject inputGeom;
    float* paintWeights;
};

struct ThreadData{
    unsigned int start;
    unsigned int end;
    unsigned int numTasks;
    TaskData* pTaskData;
};


class SmoothDeformer : public MPxDeformerNode {

public:
    SmoothDeformer();
    virtual ~SmoothDeformer();
    static void* creator();
    static MStatus initialize();
    static MStatus getInputMesh(MDataBlock& dataBlock, unsigned int geomIndex, MObject &oInputGeom);
    float* getWeightList(MDataBlock& dataBlock, unsigned int geomIndex, unsigned int numVertex);
    virtual MStatus deform(MDataBlock& dataBlock,
                            MItGeometry& itGeo,
                            const MMatrix& localToWorldMatrix,
                            unsigned int geomIndex);

    //Threading functions
    ThreadData* createThreadData(unsigned int numTasks, TaskData* pTaskData);
    static void createTasks(void* data, MThreadRootTask *pRoot);
    static MThreadRetVal threadEvaluate(void* pParam);


    static MTypeId id;

    // attributes
    static MObject aMaintain;
    static MObject aStrength;
    static MObject aSmoothBorders;
    static MObject aSmoothType;
    static MObject aLambda;
    static MObject aMu;
};

#endif