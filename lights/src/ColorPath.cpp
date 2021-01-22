#include "ColorPath.h"
#include <fstream>
#include <iterator>
#include <sstream>

ColorPath::ColorPath()
{
    _elapsedTime   = 0;
    _currentVector = 0;
}

ColorPath::ColorPath(const std::string& pathFile)
{
    _loadVectorsFromFile(pathFile);
    _elapsedTime   = 0;
    _currentVector = 0;
}

ColorPath::ColorPath(const std::vector<ColorVector>& vectors) : _vectors(vectors)
{
    _elapsedTime   = 0;
    _currentVector = 0;
}

void ColorPath::updateColor(int milliseconds, Vector4& color)
{
    if (_currentVector != -1)
    {
        if (_elapsedTime >= _vectors[_currentVector].time)
        {
            _elapsedTime = 0;
            _currentVector++;

            if (_currentVector >= _vectors.size())
            {
                _currentVector = -1;
                return;
            }
        }
        if (_elapsedTime == 0)
        {
            _diffColor = (_vectors[_currentVector].color - color) / _vectors[_currentVector].time;
            _diffAlpha = (_vectors[_currentVector].color.getw() - color.getw()) /
                         _vectors[_currentVector].time;
        }

        // Save off the alpha value to indicate if a light is on or off
        float preservedAlpha = color.getw() + (_diffAlpha * static_cast<float>(milliseconds));

        // This trashes alpha
        color = color + (_diffColor * milliseconds);

        // Push back the preserved alpha value
        color.getFlatBuffer()[3] = preservedAlpha;

        _elapsedTime += milliseconds;
    }
}

void ColorPath::resetVectorsFromFile(const std::string& pathFile)
{
    _loadVectorsFromFile(pathFile);
    _elapsedTime   = 0;
    _currentVector = 0;
}

void ColorPath::resetVectors(const std::vector<ColorVector>& vectors)
{
    _vectors       = vectors;
    _elapsedTime   = 0;
    _currentVector = 0;
}

void ColorPath::_loadVectorsFromFile(const std::string& file)
{
    _vectors.clear();
    std::ifstream infile(file);

    double      v_r, v_g, v_b, v_a;
    float       time;
    std::string line;
    while (std::getline(infile, line))
    {
        std::stringstream                  ss(line);
        std::istream_iterator<std::string> begin(ss);
        std::istream_iterator<std::string> end;
        std::vector<std::string>           vstrings(begin, end);
        std::copy(vstrings.begin(), vstrings.end(),
                  std::ostream_iterator<std::string>(std::cout, "\n"));

        v_r = ::atof(vstrings[0].c_str());
        v_g = ::atof(vstrings[1].c_str());
        v_b = ::atof(vstrings[2].c_str());
        v_a = ::atof(vstrings[3].c_str());

        time = static_cast<float>(::atof(vstrings[4].c_str()));
        Vector4 c(static_cast<float>(v_r), static_cast<float>(v_g), static_cast<float>(v_b),
                  static_cast<float>(v_a));
        _vectors.emplace_back(c, time);
    }
}
