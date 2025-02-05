#pragma once

#include "drake/util/Polynomial.h"
#include <map>

/// A scalar multi-variate polynomial containing sines and cosines.
/**
 * TrigPoly represents a Polynomial some of whose variables actually represent
 * the sines or cosines of other variables.  Sines and cosines of first-order
 * polynomials (affine combinations of variables) are decomposed into
 * polynomials of the sines and cosines of individual variables via the
 * Prosthaphaeresis formulae.
 *
 * Any variables which will appear in the arguments to trigonometric functions
 * must be declared in the "SinCosMap" (created automatically by most TrigPoly
 * constructors); attempting to, e.g., use sin(x) without first creating a
 * SinCosMap mapping for 'x' will result in an exception.
 *
 * For example:
 * \code
 * Polynomial base_x("x"), s("s"), c("c");
 * TrigPoly x(base_x, s, c)  // This "x" knows that s = sin(x)
 *                           // and that c = cos(x)
 * cout << sin(x)                     // emits "s1"
 * cout << sin(x) * x                 // emits "x1*s1"
 * cout << sin(x + x) * x             // emits "x1*s1*c1 + x1*c1*s1"
 * \endcode
 *
 * NOTE:  Certain analyses may not succeed when individual Monomials contain
 * both x and sin(x) or cos(x) terms.  This restriction is not currently
 * enforced programmatically; TODO(ggould-tri) fix this in the future.
 */
template <typename _CoefficientType = double>
class TrigPoly {
 public:
  typedef _CoefficientType CoefficientType;
  typedef Polynomial<CoefficientType> PolyType;
  struct SinCosVars {
    typename PolyType::VarType s;
    typename PolyType::VarType c;
  };
  typedef std::map<typename PolyType::VarType, SinCosVars> SinCosMap;

  /// Constructs a vacuous TrigPoly.
  TrigPoly() {}

  /// Constructs a constant TrigPoly.
  // NOLINTNEXTLINE(runtime/explicit) This conversion is desirable.
  TrigPoly(const CoefficientType& scalar) : poly(scalar) {}

  /**
   * Constructs a TrigPoly on the associated Polynomial p, but with the
   * additional information about sin and cos relations in _sin_cos_map.
   */
  TrigPoly(const PolyType& p, const SinCosMap& _sin_cos_map)
      : poly(p), sin_cos_map(_sin_cos_map) {}

  /**
   * Constructs a TrigPoly version of q, but with the additional information
   * that the variables s and c represent the sine and cosine of q.
   */
  TrigPoly(const PolyType& q, const PolyType& s, const PolyType& c) {
    if ((q.getDegree() != 1) || (s.getDegree() != 1) || (c.getDegree() != 1))
      throw std::runtime_error(
          "q, s, and c must all be simple polynomials (in the msspoly sense)");

    poly = q;
    SinCosVars sc;
    sc.s = s.getSimpleVariable();
    sc.c = c.getSimpleVariable();
    sin_cos_map[q.getSimpleVariable()] = sc;
  }

  /// Returns the underlying Polynomial for this TrigPoly.
  const PolyType& getPolynomial(void) const { return poly; }

  /// Returns the SinCosMap for this TrigPoly.
  const SinCosMap& getSinCosMap(void) const { return sin_cos_map; }

  /// A version of sin that handles TrigPoly arguments through ADL.
  /**
   * Implements sin(x) for a TrigPoly x.
   *
   * x must be of degree 0 or 1, and must contain only variables that have
   * entries in its SinCosMap.
   */
  friend TrigPoly sin(const TrigPoly& p) {
    if (p.poly.getDegree() > 1)
      throw std::runtime_error(
          "sin of polynomials with degree > 1 is not supported");

    const std::vector<typename PolyType::Monomial>& m = p.poly.getMonomials();

    if (m.size() == 1) {
      TrigPoly ret = p;
      if (m[0].terms.size() == 0) {  // then it's a constant
        ret.poly = Polynomial<CoefficientType>(sin(m[0].coefficient));
      } else {
        typename SinCosMap::iterator iter =
            ret.sin_cos_map.find(m[0].terms[0].var);
        if (iter == ret.sin_cos_map.end())
          throw std::runtime_error(
              "tried taking the sin of a variable that does not exist in my "
              "sin_cos_map");

        if (std::abs(m[0].coefficient) != (CoefficientType)1)
          throw std::runtime_error(
              "Drake:TrigPoly:PleaseImplementMe.  need to handle this case "
              "(like I do in the matlab version");

        ret.poly.subs(m[0].terms[0].var, iter->second.s);
      }
      return ret;
    }

    // otherwise handle the multi-monomial case recursively
    // sin(a+b+...) = sin(a)cos(b+...) + cos(a)sin(b+...)
    Polynomial<CoefficientType> pa(m[0].coefficient, m[0].terms),
        pb(m.begin() + 1, m.end());
    TrigPoly a(pa, p.sin_cos_map), b(pb, p.sin_cos_map);
    return sin(a) * cos(b) + cos(a) * sin(b);
  }

  /// A version of cos that handles TrigPoly arguments through ADL.
  /**
   * Implements cos(x) for a TrigPoly x.
   *
   * x must be of degree 0 or 1, and must contain only variables that have
   * entries in its SinCosMap.
   */
  friend TrigPoly cos(const TrigPoly& p) {
    if (p.poly.getDegree() > 1)
      throw std::runtime_error(
          "cos of polynomials with degree > 1 is not supported");

    const std::vector<typename PolyType::Monomial>& m = p.poly.getMonomials();

    if (m.size() == 1) {
      TrigPoly ret = p;
      if (m[0].terms.size() == 0) {  // then it's a constant
        ret.poly = Polynomial<CoefficientType>(cos(m[0].coefficient));
      } else {
        typename SinCosMap::iterator iter =
            ret.sin_cos_map.find(m[0].terms[0].var);
        if (iter == ret.sin_cos_map.end())
          throw std::runtime_error(
              "tried taking the sin of a variable that does not exist in my "
              "sin_cos_map");

        if (std::abs(m[0].coefficient) != (CoefficientType)1)
          throw std::runtime_error(
              "Drake:TrigPoly:PleaseImplementMe.  need to handle this case "
              "(like I do in the matlab version");

        ret.poly.subs(m[0].terms[0].var, iter->second.c);
        if (m[0].coefficient == (CoefficientType)-1) {
          ret *= -1;
        }  // cos(-q) => cos(q) => c (instead of -c)
      }
      return ret;
    }

    // otherwise handle the multi-monomial case recursively
    // cos(a+b+...) = cos(a)cos(b+...) - sin(a)sin(b+...)
    Polynomial<CoefficientType> pa(m[0].coefficient, m[0].terms),
        pb(m.begin() + 1, m.end());
    TrigPoly a(pa, p.sin_cos_map), b(pb, p.sin_cos_map);
    return cos(a) * cos(b) - sin(a) * sin(b);
  }

  TrigPoly& operator+=(const TrigPoly& other) {
    poly += other.poly;
    sin_cos_map.insert(other.sin_cos_map.begin(), other.sin_cos_map.end());
    return *this;
  }

  TrigPoly& operator-=(const TrigPoly& other) {
    poly -= other.poly;
    sin_cos_map.insert(other.sin_cos_map.begin(), other.sin_cos_map.end());
    return *this;
  }

  TrigPoly& operator*=(const TrigPoly& other) {
    poly *= other.poly;
    sin_cos_map.insert(other.sin_cos_map.begin(), other.sin_cos_map.end());
    return *this;
  }

  TrigPoly& operator+=(const CoefficientType& scalar) {
    poly += scalar;
    return *this;
  }

  TrigPoly& operator-=(const CoefficientType& scalar) {
    poly -= scalar;
    return *this;
  }

  TrigPoly& operator*=(const CoefficientType& scalar) {
    poly *= scalar;
    return *this;
  }

  TrigPoly& operator/=(const CoefficientType& scalar) {
    poly /= scalar;
    return *this;
  }

  const TrigPoly operator+(const TrigPoly& other) const {
    TrigPoly ret = *this;
    ret += other;
    return ret;
  }

  const TrigPoly operator-(const TrigPoly& other) const {
    TrigPoly ret = *this;
    ret -= other;
    return ret;
  }

  const TrigPoly operator-() const {
    TrigPoly ret = -(*this);
    return ret;
  }

  const TrigPoly operator*(const TrigPoly& other) const {
    TrigPoly ret = *this;
    ret *= other;
    return ret;
  }

  friend const TrigPoly operator+(const TrigPoly& p,
                                  const CoefficientType& scalar) {
    TrigPoly ret = p;
    ret += scalar;
    return ret;
  }

  friend const TrigPoly operator+(const CoefficientType& scalar,
                                  const TrigPoly& p) {
    TrigPoly ret = p;
    ret += scalar;
    return ret;
  }

  friend const TrigPoly operator-(const TrigPoly& p,
                                  const CoefficientType& scalar) {
    TrigPoly ret = p;
    ret -= scalar;
    return ret;
  }

  friend const TrigPoly operator-(const CoefficientType& scalar,
                                  const TrigPoly& p) {
    TrigPoly ret = -p;
    ret += scalar;
    return ret;
  }

  friend const TrigPoly operator*(const TrigPoly& p,
                                  const CoefficientType& scalar) {
    TrigPoly ret = p;
    ret *= scalar;
    return ret;
  }
  friend const TrigPoly operator*(const CoefficientType& scalar,
                                  const TrigPoly& p) {
    TrigPoly ret = p;
    ret *= scalar;
    return ret;
  }

  const TrigPoly operator/(const CoefficientType& scalar) const {
    TrigPoly ret = *this;
    ret /= scalar;
    return ret;
  }

  friend std::ostream& operator<<(std::ostream& os,
                                  const TrigPoly<CoefficientType>& tp) {
    os << tp.poly;
    return os;
  }

 private:
  PolyType poly;
  SinCosMap sin_cos_map;
};

template <typename CoefficientType, int Rows, int Cols>
std::ostream& operator<<(
    std::ostream& os,
    const Eigen::Matrix<TrigPoly<CoefficientType>, Rows, Cols>& tp_mat) {
  Eigen::Matrix<Polynomial<CoefficientType>, Rows, Cols> poly_mat(
      tp_mat.rows(), tp_mat.cols());
  for (int i = 0; i < poly_mat.size(); i++) {
    poly_mat(i) = tp_mat(i).getPolynomial();
  }
  os << poly_mat;
  return os;
}

typedef TrigPoly<double> TrigPolyd;
