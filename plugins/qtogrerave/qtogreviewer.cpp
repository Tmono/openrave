#include "qtogreviewer.h"

#include <mutex>
#include <condition_variable>

#include <OGRE/OgreMeshManager2.h>
#include <OGRE/OgreMesh2.h>
#include <OGRE/OgreItem.h>
#include <OGRE/OgreSubMesh2.h>

namespace qtogrerave {

QtOgreViewer::QtOgreViewer(EnvironmentBasePtr penv, std::istream& sinput) : ViewerBase(penv) {
    __description = ":Interface Author: Woody Chow\n\nQt/Ogre Viewer";

    RegisterCommand("StartViewerLoop", boost::bind(&QtOgreViewer::startmainloop, this, _1, _2),
                    "starts the viewer sync loop and shows the viewer. expects someone else will call the qapplication exec fn");
}

QtOgreViewer::~QtOgreViewer() {
    quitmainloop();
}

int QtOgreViewer::main(bool bShow)
{
    if( !QApplication::instance() ) {
        throw OPENRAVE_EXCEPTION_FORMAT0("need a valid QApplication before viewer loop is run", ORE_InvalidState);
    }

    // TODO: Take care of bshow
    _ogreWindow = boost::make_shared<QtOgreWindow>();
    // _ogreWindow->show();
    _ogreWindow->showMaximized();
    QGuiApplication::instance()->exec();
}

bool QtOgreViewer::startmainloop(std::ostream& sout, std::istream& sinput)
{
    QGuiApplication::instance()->exec();
}

void QtOgreViewer::quitmainloop()
{
    QApplication::quit();
}

GraphHandlePtr QtOgreViewer::plot3(const float* ppoints, int numPoints, int stride, float fPointSize, const RaveVector<float>& color, int drawstyle)
{
    // From my experience, graphics driver will convert vec3 to vec4 if vec3 is provided
    // TODO: Benchmark?
    float *vpoints = reinterpret_cast<float*>(OGRE_MALLOC_SIMD(
        4 * numPoints *  sizeof(float), Ogre::MEMCATEGORY_GEOMETRY));
    for (int64_t i = 0; i < 4 * numPoints; i += 4) {
        vpoints[i] = ppoints[0];
        vpoints[i + 1] = ppoints[1];
        vpoints[i + 2] = ppoints[2];
        vpoints[i + 3] = 1.0f;
        ppoints = (float*)((char*)ppoints + stride);
    }

    // TODO: Calculate bounds

    std::mutex cv_m;
    std::condition_variable cv;
    OgreHandlePtr handle = boost::make_shared<OgreHandle>();

    _ogreWindow->QueueRenderingUpdate(boost::make_shared<QtOgreWindow::GUIThreadFunction>([this, &cv_m, &cv, &handle, vpoints, numPoints, stride, fPointSize, color, drawstyle]() {
        std::lock_guard<std::mutex> lk(cv_m);
        Ogre::RenderSystem *renderSystem = _ogreWindow->GetRoot()->getRenderSystem();
        Ogre::VaoManager *vaoManager = renderSystem->getVaoManager();

        Ogre::VertexElement2Vec vertexElements;
        vertexElements.push_back(Ogre::VertexElement2(Ogre::VET_FLOAT4, Ogre::VES_POSITION));

        Ogre::VertexBufferPacked* vertexBuffer = nullptr;
        try
        {
            //Create the actual vertex buffer.
            vertexBuffer = vaoManager->createVertexBuffer(
                vertexElements, numPoints,
                Ogre::BT_IMMUTABLE,
                vpoints,
                true
            );
        }
        catch(Ogre::Exception &e)
        {
            OGRE_FREE_SIMD(vertexBuffer, Ogre::MEMCATEGORY_GEOMETRY);
            vertexBuffer = nullptr;
            throw e; // TODO: throw openrave exception
        }

        Ogre::VertexArrayObject* vao = vaoManager->createVertexArrayObject(
            {vertexBuffer},
            nullptr, // Points do not need index buffer
            Ogre::OT_POINT_LIST);

        Ogre::SceneNode* parentNode = _ogreWindow->GetMiscDrawNode();
        Ogre::SceneNode* node = parentNode->createChildSceneNode();
        // Do the mesh and mesh group name have to be unique?
        Ogre::MeshPtr mesh = Ogre::MeshManager::getSingleton().createManual("plot3", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
        Ogre::SubMesh* submesh = mesh->createSubMesh();
        submesh->mVao[Ogre::VpNormal].push_back(vao);
        submesh->mVao[Ogre::VpShadow].push_back(vao);
        //Set the bounds to get frustum culling and LOD to work correctly.
        // mesh->_setBounds( Ogre::Aabb( Ogre::Vector3::ZERO, Ogre::Vector3(256, 128, 256)), false );
        // mesh->_setBoundingSphereRadius( 128.0f );
        Ogre::SceneManager *sceneManager = node->getCreator();
        Ogre::Item *item = sceneManager->createItem(mesh);
        node->attachObject(item);
        printf("---------------------node %p %p\n", node, node->getParentSceneNode());
        handle->_node = node;

        cv.notify_all();
    }));

    {
        std::unique_lock<std::mutex> lk(cv_m);
        cv.wait(lk);
    }

    return handle; // This segfaults
}

GraphHandlePtr QtOgreViewer::drawlinestrip(const float* ppoints, int numPoints, int stride, float fwidth, const RaveVector<float>& color)
{
    // From my experience, graphics driver will convert vec3 to vec4 if vec3 is provided
    // TODO: Benchmark?
    std::vector<float> vpoints(4 * numPoints);
    for (int64_t i = 0; i < 4 * numPoints; i += 4) {
        vpoints[i] = ppoints[0];
        vpoints[i + 1] = ppoints[1];
        vpoints[i + 2] = ppoints[2];
        vpoints[i + 3] = 1.0f;
        ppoints = (float*)((char*)ppoints + stride);
    }

    _ogreWindow->QueueRenderingUpdate(boost::make_shared<QtOgreWindow::GUIThreadFunction>([this, vpoints, numPoints, stride, fwidth, color]() {
        Ogre::RenderSystem *renderSystem = _ogreWindow->GetRoot()->getRenderSystem();
        Ogre::VaoManager *vaoManager = renderSystem->getVaoManager();

        Ogre::VertexElement2Vec vertexElements;
        vertexElements.push_back(Ogre::VertexElement2(Ogre::VET_FLOAT4, Ogre::VES_POSITION));

        Ogre::VertexBufferPacked* vertexBuffer = nullptr;
        try
        {
            //Create the actual vertex buffer.
            vertexBuffer = vaoManager->createVertexBuffer(
                vertexElements, numPoints,
                Ogre::BT_IMMUTABLE,
                const_cast<float*>(vpoints.data()), // const when keepAsShadow is false
                false                               // keepAsShadow
            );
        }
        catch(Ogre::Exception &e)
        {
            OGRE_FREE_SIMD(vertexBuffer, Ogre::MEMCATEGORY_GEOMETRY);
            vertexBuffer = nullptr;
            throw e; // TODO: throw openrave exception
        }

        Ogre::VertexArrayObject* vao = vaoManager->createVertexArrayObject(
            {vertexBuffer},
            nullptr, // Do not need index buffer
            Ogre::OT_LINE_STRIP);

        Ogre::SceneNode* parentNode = _ogreWindow->GetMiscDrawNode();
        Ogre::SceneNode* node = parentNode->createChildSceneNode();
        // Do the mesh and mesh group name have to be unique?
        Ogre::MeshPtr mesh = Ogre::MeshManager::getSingleton().createManual("linelist", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
        Ogre::SubMesh* submesh = mesh->createSubMesh();
        submesh->mVao[Ogre::VpNormal].push_back(vao);
        submesh->mVao[Ogre::VpShadow].push_back(vao);
        //Set the bounds to get frustum culling and LOD to work correctly.
        // mesh->_setBounds( Ogre::Aabb( Ogre::Vector3::ZERO, Ogre::Vector3(256, 128, 256)), false );
        // mesh->_setBoundingSphereRadius( 128.0f );
        Ogre::SceneManager *sceneManager = node->getCreator();
        Ogre::Item *item = sceneManager->createItem(mesh);
        node->attachObject(item);
        // *handle = OgreHandle(node); // fix later
    }));
}

GraphHandlePtr QtOgreViewer::drawlinelist(const float* ppoints, int numPoints, int stride, float fwidth, const RaveVector<float>& color)
{
    // From my experience, graphics driver will convert vec3 to vec4 if vec3 is provided
    // TODO: Benchmark?
    std::vector<float> vpoints(4 * numPoints);
    for (int64_t i = 0; i < 4 * numPoints; i += 4) {
        vpoints[i] = ppoints[0];
        vpoints[i + 1] = ppoints[1];
        vpoints[i + 2] = ppoints[2];
        vpoints[i + 3] = 1.0f;
        ppoints = (float*)((char*)ppoints + stride);
    }

    _ogreWindow->QueueRenderingUpdate(boost::make_shared<QtOgreWindow::GUIThreadFunction>([this, vpoints, numPoints, stride, fwidth, color]() {
        Ogre::RenderSystem *renderSystem = _ogreWindow->GetRoot()->getRenderSystem();
        Ogre::VaoManager *vaoManager = renderSystem->getVaoManager();

        Ogre::VertexElement2Vec vertexElements;
        vertexElements.push_back(Ogre::VertexElement2(Ogre::VET_FLOAT4, Ogre::VES_POSITION));

        Ogre::VertexBufferPacked* vertexBuffer = nullptr;
        try
        {
            //Create the actual vertex buffer.
            vertexBuffer = vaoManager->createVertexBuffer(
                vertexElements, numPoints,
                Ogre::BT_IMMUTABLE,
                const_cast<float*>(vpoints.data()), // const when keepAsShadow is false
                false                               // keepAsShadow
            );
        }
        catch(Ogre::Exception &e)
        {
            OGRE_FREE_SIMD(vertexBuffer, Ogre::MEMCATEGORY_GEOMETRY);
            vertexBuffer = nullptr;
            throw e; // TODO: throw openrave exception
        }

        Ogre::VertexArrayObject* vao = vaoManager->createVertexArrayObject(
            {vertexBuffer},
            nullptr, // Do not need index buffer
            Ogre::OT_LINE_LIST);

        Ogre::SceneNode* parentNode = _ogreWindow->GetMiscDrawNode();
        Ogre::SceneNode* node = parentNode->createChildSceneNode();
        // Do the mesh and mesh group name have to be unique?
        Ogre::MeshPtr mesh = Ogre::MeshManager::getSingleton().createManual("linelist", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
        Ogre::SubMesh* submesh = mesh->createSubMesh();
        submesh->mVao[Ogre::VpNormal].push_back(vao);
        submesh->mVao[Ogre::VpShadow].push_back(vao);
        //Set the bounds to get frustum culling and LOD to work correctly.
        // mesh->_setBounds( Ogre::Aabb( Ogre::Vector3::ZERO, Ogre::Vector3(256, 128, 256)), false );
        // mesh->_setBoundingSphereRadius( 128.0f );
        Ogre::SceneManager *sceneManager = node->getCreator();
        Ogre::Item *item = sceneManager->createItem(mesh);
        node->attachObject(item);
        // *handle = OgreHandle(node); // fix later
    }));
}

GraphHandlePtr QtOgreViewer::drawbox(const RaveVector<float>& vpos, const RaveVector<float>& vextents)
{
    _ogreWindow->QueueRenderingUpdate(boost::make_shared<QtOgreWindow::GUIThreadFunction>([this, &vpos, &vextents]() {
        // TODO: Use vertex buffer?
        Ogre::SceneNode* parentNode = _ogreWindow->GetMiscDrawNode();
        Ogre::SceneNode* node = parentNode->createChildSceneNode();
        Ogre::v1::Entity* cube = node->getCreator()->createEntity(Ogre::SceneManager::PT_CUBE);
        cube->setDatablock(_ogreWindow->datablockhack);
        node->attachObject(cube);
        node->setPosition(Ogre::Vector3(vpos.x, vpos.y, vpos.z));
        node->setScale(Ogre::Vector3(vextents.x, vextents.y, vextents.z)); // <--------- is this extents?
        // *handle = OgreHandle(node); // fix later
    }));
    // Ogre::SceneNode* parentNode = _ogreWindow->GetMiscDrawNode();
    //     Ogre::SceneNode* node = parentNode->createChildSceneNode();
    //     Ogre::v1::Entity* cube = node->getCreator()->createEntity(Ogre::SceneManager::PT_CUBE);
    //     cube->setDatablock(_ogreWindow->datablockhack);
    //     node->attachObject(cube);
    //     node->setPosition(Ogre::Vector3(vpos.x, vpos.y, vpos.z));
    //     node->setScale(Ogre::Vector3(vextents.x, vextents.y, vextents.z)); // <--------- is this extents?

    //return boost::make_shared<OgreHandle>(node);
    //return boost::make_shared<OgreHandle>(node);
}

ViewerBasePtr CreateQtOgreViewer(EnvironmentBasePtr penv, std::istream& sinput)
{
    return boost::make_shared<QtOgreViewer>(penv, sinput);
}

}; // namespace qtogrerave