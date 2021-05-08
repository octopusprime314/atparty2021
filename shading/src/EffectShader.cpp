#include "EffectShader.h"
#include "EngineManager.h"
#include "HLSLShader.h"
#include "Light.h"
#include "ViewEventDistributor.h"

EffectShader::EffectShader( std::string shaderName)
{
    std::vector<DXGI_FORMAT>* formats = new std::vector<DXGI_FORMAT>();

    if (shaderName.find("fireShader") != std::string::npos)
    {
        formats->push_back(DXGI_FORMAT_R8G8B8A8_UNORM);
        formats->push_back(DXGI_FORMAT_D32_FLOAT);
    }
    _shader = new HLSLShader(shaderName, "", formats);
}

EffectShader::~EffectShader() {}

void EffectShader::runShader(Effect* effectObject, float seconds)
{
    // LOAD IN SHADER
    _shader->bind();

    _shader->bindAttributes(nullptr, false);
    
    auto cameraMVP = effectObject->getCameraMVP();
    
    auto view = EngineManager::instance()->getViewManager()->getView();
    _shader->updateData("view", view.getFlatBuffer());

    auto projection = EngineManager::instance()->getViewManager()->getProjection();
    _shader->updateData("projection", projection.getFlatBuffer());

    if (effectObject->getType() == EffectType::Fire || effectObject->getType() == EffectType::Smoke)
    {

        Light* light      = static_cast<Light*>(effectObject);
        auto   planeScale = Matrix::scale(1.0f);

        Vector4 scale = light->getScale();

        if (effectObject->getType() == EffectType::Smoke)
        {
            seconds /= 4.0f;
            planeScale = Matrix::scale(scale.getx(), scale.gety(), scale.getz());
        }
        if (effectObject->getType() == EffectType::Fire)
        {
            seconds /= 2.0f;
            planeScale = Matrix::scale(scale.getx(), scale.gety(), scale.getz());
        }
        auto lightMVP = light->getLightMVP();

        // Pass the type of fire to the shader to simulate i.e. candle light or bon fire
        int fireType = 2;
        _shader->updateData("fireType", &fireType);

        float* color = light->getColor().getFlatBuffer();
        _shader->updateData("fireColor", color);

        auto viewNoTrans                = cameraMVP.getViewMatrix();
        viewNoTrans.getFlatBuffer()[3]  = 0.0;
        viewNoTrans.getFlatBuffer()[7]  = 0.0;
        viewNoTrans.getFlatBuffer()[11] = 0.0;

        auto model = lightMVP.getModelMatrix() * planeScale;
        _shader->updateData("model", model.getFlatBuffer());

        auto inverseViewNoTrans = viewNoTrans.inverse();
        _shader->updateData("inverseViewNoTrans", inverseViewNoTrans.getFlatBuffer());
    }

    // Pass game time to shader
    _shader->updateData("time", &seconds);

    _shader->draw(0, 1, 4);
    
    _shader->unbindAttributes();
    _shader->unbind();
}