#include "AnimatedModel.h"
#include "ShaderBroker.h"

AnimatedModel::AnimatedModel(std::string name)
    : Model(name, ModelClass::AnimatedModelType), _currentAnimation(0)
{

    // First create an animation object
    Animation* animation = AnimationBuilder::buildAnimation();
    // Push animation onto the animated model
    addAnimation(animation);

    // Grab skin animation transforms
    //_fbxLoader->loadAnimatedModel(this, _fbxLoader->getScene()->GetRootNode());

    // Build up each animation frame's vertices and normals
    //_fbxLoader->buildAnimationFrames(this, animation->getSkins());

    std::string colliderName = name.substr(0, name.find_last_of("/") + 1) + "collider.fbx";
    // Load in geometry fbx object
    //FbxLoader geometryLoader(colliderName);
    //// Populate model with fbx file data and recursivelty search with the root node of the scene
    //geometryLoader.loadGeometry(this, geometryLoader.getScene()->GetRootNode());

    //_fbxLoader->buildCollisionAABB(this);

    // Override default shader with a bone animation shader
    //_shaderProgram =
    //    static_cast<AnimationShader*>(ShaderBroker::instance()->getShader("animatedShader"));

    //_currBones = _animations[_currentAnimation]->getBones();

    //_vao[0]->createVAO(&_renderBuffers, ModelClass::AnimatedModelType/*,
    //                   _animations[_currentAnimation]*/);
}

AnimatedModel::~AnimatedModel() {}

void AnimatedModel::updateModel(Model* model)
{
    std::lock_guard<std::mutex> lockGuard(_updateLock);

    auto animatedModel   = static_cast<AnimatedModel*>(model);
    this->_indexContext  = animatedModel->_indexContext;
    this->_weightContext = animatedModel->_weightContext;

    for (auto animation : this->_animations)
    {
        delete animation;
    }

    this->_animations = animatedModel->_animations;

    Model::updateModel(model);
}

void AnimatedModel ::updateAnimation()
{
    // If animation update request was received from an asynchronous event then send vbo to gpu
    if (_animationUpdateRequest)
    {

        // Set bone animation frame
        //_currBones = &_jointMatrices;//_animations[_currentAnimation]->getBones();
        // Set animation to the next frame
        _animations[_currentAnimation]->nextFrame();
        _keyFrame++;

        _keyFrame = _keyFrame % _frames;

        _updateLock.lock();
        _animationUpdateRequest = false;
        _updateLock.unlock();
    }
}

void AnimatedModel::setJointMatrices(std::vector<Matrix> jointMatrices) { _jointMatrices = jointMatrices; }
void AnimatedModel::setJoints(std::vector<float> joints) { _joints = joints; }
void AnimatedModel::setWeights(std::vector<float> weights) { _weights = weights; }
void AnimatedModel::setKeyFrames(int frames) { _frames = frames; }

std::vector<Matrix> AnimatedModel::getJointMatrices()
{
   auto jointCount = _jointMatrices.size() / _frames;
   std::vector<Matrix> matricesForFrame;
   for (int i = 0; i < jointCount; i++)
   {
       matricesForFrame.push_back(_jointMatrices[i + (_keyFrame * jointCount)]);
   }

   return matricesForFrame;
}
std::vector<float>*    AnimatedModel::getJoints() { return &_joints; }
std::vector<float>* AnimatedModel::getWeights() { return &_weights; }


void AnimatedModel::addAnimation(Animation* animation) { _animations.push_back(animation); }

Animation* AnimatedModel::getAnimation() { return _animations.back(); }

void AnimatedModel::triggerNextFrame()
{
    // Coordinate loading new animation frame to gpu
    _updateLock.lock();
    _animationUpdateRequest = true;
    _updateLock.unlock();
}

uint32_t AnimatedModel::getIndexContext() { return _indexContext; }

uint32_t AnimatedModel::getWeightContext() { return _weightContext; }

std::vector<Matrix>* AnimatedModel::getBones() { return _currBones; }
