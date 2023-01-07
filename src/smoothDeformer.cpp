#include "smoothDeformer.h"

MTypeId SmoothDeformer::id(0x000494f2);


MObject SmoothDeformer::aStrength;
MObject SmoothDeformer::aSmoothBorders;
MObject SmoothDeformer::aMaintain;
MObject SmoothDeformer::aSmoothType;
MObject SmoothDeformer::aLambda;
MObject SmoothDeformer::aMu;


SmoothDeformer::SmoothDeformer() {
    CHECK_MSTATUS(MThreadPool::init());
}


SmoothDeformer::~SmoothDeformer() {
    MThreadPool::release();
}


void* SmoothDeformer::creator() {
    return new SmoothDeformer();
}


MStatus SmoothDeformer::getInputMesh(MDataBlock& dataBlock, unsigned int geomIndex, MObject &oInputGeom) {
    // Gets the input mesh that will be deformed.

    MStatus status;
    //using output to prevent Maya from triggering a dirty propagation
    MArrayDataHandle hInput = dataBlock.outputArrayValue(input, &status);
    CHECK_MSTATUS(status);
    status = hInput.jumpToElement(geomIndex);
    oInputGeom = hInput.outputValue().child(inputGeom).asMesh();
    return status;
}



float* SmoothDeformer::getWeightList(MDataBlock& dataBlock, unsigned int geomIndex, unsigned int numVertex){
    // Returns a list with the weight of all the vertex in the geo whose index is geomIndex.

    float* paintWeights = new float[numVertex];
    for (unsigned int index=0; index<numVertex; ++index) {
        //Current point and weight
        paintWeights[index] = weightValue(dataBlock, geomIndex, index);
    }
    return paintWeights;
}



MStatus SmoothDeformer::deform(MDataBlock& dataBlock, MItGeometry& itGeo, const MMatrix& localToWorldMatrix, unsigned int geomIndex){
    // Computes the deformation.

    MStatus status;

    //Get the input envelope
    float envelopeValue = dataBlock.inputValue(envelope, &status).asFloat();
    CHECK_MSTATUS(status);
    if (!envelopeValue) {
        return MS::kSuccess;
    }

    //Get the input steps
    int iterations = dataBlock.inputValue(aStrength, &status).asInt();
    CHECK_MSTATUS(status);
    if (!iterations) {
        return MS::kSuccess;
    }

    //Get the input maintain
    float maintainValue = dataBlock.inputValue(aMaintain, &status).asFloat();
    CHECK_MSTATUS(status);

    //Get the input smooth borders
    short smoothBorders = dataBlock.inputValue(aSmoothBorders, &status).asShort();
    CHECK_MSTATUS(status);

    //Get the input smooth type
    short smoothType = dataBlock.inputValue(aSmoothType, &status).asShort();
    CHECK_MSTATUS(status);

    //Get the input lambda
    float lambdaInput = dataBlock.inputValue(aLambda, &status).asFloat();
    CHECK_MSTATUS(status);

    //Get the input mu
    float mu = dataBlock.inputValue(aMu, &status).asFloat();
    CHECK_MSTATUS(status);

    MObject inputGeomObj;
    getInputMesh(dataBlock, geomIndex, inputGeomObj);

    float* paintWeights = getWeightList(dataBlock, geomIndex, itGeo.count());

    TaskData taskData;
    itGeo.allPositions(taskData.points);
    itGeo.allPositions(taskData.newPoints);
    taskData.envelope = envelopeValue;
    taskData.inputGeom = inputGeomObj;
    taskData.iterations = iterations;
    taskData.maintainValue = maintainValue;
    taskData.smoothBorders = smoothBorders;
    taskData.lambda = lambdaInput;
    taskData.paintWeights = paintWeights;

    //Get the normals from each vertex
    MFnMesh fnMesh(inputGeomObj, &status);
    fnMesh.getVertexNormals(true, taskData.normals, MSpace::kTransform);


    for (int type = 0; type < smoothType+1; type++){
        for (int itGeo = 0; itGeo < iterations; itGeo++) {
            unsigned int numTasks = MThreadUtils::getNumThreads;
            ThreadData* pThreadData = createThreadData(numTasks, &taskData);
            MThreadPool::newParallelRegion(createTasks, (void*)pThreadData);
            taskData.points = taskData.newPoints;
            delete [] pThreadData;
        }
        taskData.lambda = -1 * lambdaInput + mu;
    }
    //Set new positions:
    itGeo.setAllPositions(taskData.points);

    return MS::kSuccess;
}



ThreadData* SmoothDeformer::createThreadData(unsigned int numTasks, TaskData* pTaskData){
    //Creates the thread data

    ThreadData* pThreadData = new ThreadData[numTasks];
    unsigned int numPoints = pTaskData->points.length();
    unsigned int taskLength = (numPoints+numTasks-1)/numTasks;
    unsigned int start = 0;
    unsigned int end = taskLength;

    int lastTask = numTasks-1;
    for(unsigned int i=0; i<numTasks; i++){
        if(i==lastTask){
            end = numPoints;
        }
        pThreadData[i].start = start;
        pThreadData[i].end = end;
        pThreadData[i].numTasks = numTasks;
        pThreadData[i].pTaskData = pTaskData;

        start += taskLength;
        end += taskLength;
    }
    return pThreadData;
}



void SmoothDeformer::createTasks(void* pData, MThreadRootTask* pRoot){
    //Creates the thread tasks

    ThreadData* pThreadData = (ThreadData*)pData;
    if(pThreadData){
        int numTasks = pThreadData->numTasks;
        for(int i=0; i<numTasks; ++i){
            MThreadPool::createTask(threadEvaluate, (void*)&pThreadData[i], pRoot);
        }
        MThreadPool::executeAndJoin(pRoot);
    }
}


MThreadRetVal SmoothDeformer::threadEvaluate(void* pParam){
    //Executes the smooth operation in multiple threads

    MStatus status;
    ThreadData* pThreadData = (ThreadData*)(pParam);
    TaskData* pTaskData = pThreadData->pTaskData;

    unsigned int start = pThreadData->start;
    unsigned int end = pThreadData->end;

    MPointArray& points = pTaskData->points;
    float envelope = pTaskData->envelope;
    int iterations = pTaskData->iterations;
    float maintainValue = pTaskData->maintainValue;
    short smoothBorders = pTaskData->smoothBorders;
    float lambda = pTaskData->lambda;

    //Get polygon iteration tool
    MItMeshVertex vertexIt = MItMeshVertex(pTaskData->inputGeom);

    MPoint averagePoint, offsetPoint, newPoint;
    for(unsigned int index=start; index<end; ++index){
        if(index>=points.length()){
            break;
        }
        //Set iterator index
        int prevPtr = 0;
        vertexIt.setIndex(index, prevPtr);

        //Don't smooth if the vertex is boundary and don't want smooth borders
        if(vertexIt.onBoundary() && !smoothBorders){
            continue;
        }

        //Get connected vertices, vertex neighbors
        MIntArray connectedVertex;
        vertexIt.getConnectedVertices(connectedVertex);

        //Get total position
        MPoint totalPos = MPoint();
        for (unsigned int conVertex=0; conVertex<connectedVertex.length(); conVertex++) {
            totalPos = totalPos + points[connectedVertex[conVertex]];
        }

        //Averaged position
        averagePoint = totalPos / connectedVertex.length();
        //Calculate offset
        offsetPoint = (averagePoint - points[index]) * envelope * pTaskData->paintWeights[index] * lambda;

        //Normal distance vector
        MPoint normalPoint(pTaskData->normals[index]);
        double amount = sqrt(pow(offsetPoint.x, 2) + pow(offsetPoint.y, 2) + pow(offsetPoint.z, 2));
        //Calculate new position
        newPoint = points[index] + offsetPoint + (normalPoint * amount * maintainValue);

        //Update new position
        pTaskData->newPoints.set(newPoint, index);
    }
    return 0;
}



MStatus SmoothDeformer::initialize(){
    //Initialize the node, attributes.

    MFnNumericAttribute nAttr;
    MFnEnumAttribute enumAttr;

    //Smooth type
    aSmoothType = enumAttr.create("smoothAlgorithm", "smoothAlgorithm", 0);
    enumAttr.addField("Laplacian", 0);
    enumAttr.addField("Taubin", 1);
    enumAttr.setKeyable(false);
    enumAttr.setChannelBox(true);
    CHECK_MSTATUS(addAttribute(aSmoothType));
    CHECK_MSTATUS(attributeAffects(aSmoothType, outputGeom));

    //Strength
    aStrength = nAttr.create("strength", "st", MFnNumericData::kInt);
    nAttr.setKeyable(true);
    nAttr.setMin(0.0);
    nAttr.setMax(300);
    nAttr.setDefault(0.0);
    CHECK_MSTATUS(addAttribute(aStrength));
    CHECK_MSTATUS(attributeAffects(aStrength, outputGeom));

    //Smooth borders
    aSmoothBorders = enumAttr.create("smoothBorders", "smb", 0);
    enumAttr.setKeyable(false);
    enumAttr.setChannelBox(true);
    enumAttr.addField("Off", 0);
    enumAttr.addField("On", 1);
    CHECK_MSTATUS(addAttribute(aSmoothBorders));
    CHECK_MSTATUS(attributeAffects(aSmoothBorders, outputGeom));

    //Maintain Volume
    aMaintain = nAttr.create("maintainVolume", "mtn", MFnNumericData::kFloat);
    nAttr.setKeyable(true);
    nAttr.setMin(0.0);
    nAttr.setMax(1.0);
    nAttr.setDefault(0);
    CHECK_MSTATUS(addAttribute(aMaintain));
    CHECK_MSTATUS(attributeAffects(aMaintain, outputGeom));

    //Lambda
    aLambda = nAttr.create("lambda", "lam", MFnNumericData::kFloat);
    nAttr.setKeyable(true);
    nAttr.setChannelBox(true);
    nAttr.setMin(0.0);
    nAttr.setMax(1.0);
    nAttr.setDefault(0.5);
    CHECK_MSTATUS(addAttribute(aLambda));
    CHECK_MSTATUS(attributeAffects(aLambda, outputGeom));

    //Mu
    aMu = nAttr.create("mu", "mu", MFnNumericData::kFloat);
    nAttr.setKeyable(true);
    nAttr.setChannelBox(true);
    nAttr.setMin(0.0);
    nAttr.setMax(1.0);
    nAttr.setDefault(0.003);
    CHECK_MSTATUS(addAttribute(aMu));
    CHECK_MSTATUS(attributeAffects(aMu, outputGeom));

    //Make weight paintable
    MGlobal::executeCommand("makePaintable -attrType multiFloat -sm deformer smoothDeformer weights;");
    return MS::kSuccess;
}