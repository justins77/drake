#include <typeinfo>
#include "drake/solvers/MathematicalProgram.h"
#include "drake/solvers/NloptSolver.h"
#include "drake/solvers/Optimization.h"
#include "drake/solvers/SnoptSolver.h"
#include "drake/util/eigen_matrix_compare.h"
#include "drake/util/Polynomial.h"
#include "drake/util/testUtil.h"
#include "gtest/gtest.h"

using Eigen::Dynamic;
using Eigen::Ref;
using Eigen::Matrix;
using Eigen::Matrix2d;
using Eigen::Matrix4d;
using Eigen::MatrixXd;
using Eigen::Vector2d;
using Eigen::Vector3d;
using Eigen::Vector4d;
using Eigen::VectorXd;

using Drake::Constraint;
using Drake::TaylorVecXd;
using Drake::VecIn;
using Drake::Vector1d;
using Drake::VecOut;
using Drake::MathematicalProgramSolverInterface;
using Drake::NloptSolver;
using Drake::OptimizationProblem;
using Drake::BoundingBoxConstraint;
using Drake::SnoptSolver;
using Drake::LinearComplementarityConstraint;
using drake::util::MatrixCompareType;

namespace drake {
namespace solvers {
namespace {

struct Movable {
  Movable() = default;
  Movable(Movable&&) = default;
  Movable(Movable const&) = delete;
  static size_t numInputs() { return 1; }
  static size_t numOutputs() { return 1; }
  template <typename ScalarType>
  void eval(VecIn<ScalarType> const&, VecOut<ScalarType>&) const {}
};

struct Copyable {
  Copyable() = default;
  Copyable(Copyable&&) = delete;
  Copyable(Copyable const&) = default;
  static size_t numInputs() { return 1; }
  static size_t numOutputs() { return 1; }
  template <typename ScalarType>
  void eval(VecIn<ScalarType> const&, VecOut<ScalarType>&) const {}
};

struct Unique {
  Unique() = default;
  Unique(Unique&&) = delete;
  Unique(Unique const&) = delete;
  static size_t numInputs() { return 1; }
  static size_t numOutputs() { return 1; }
  template <typename ScalarType>
  void eval(VecIn<ScalarType> const&, VecOut<ScalarType>&) const {}
};

TEST(testOptimizationProblem, testAddFunction) {
  OptimizationProblem prog;
  prog.AddContinuousVariables(1);

  Movable movable;
  prog.AddCost(std::move(movable));
  prog.AddCost(Movable());

  Copyable copyable;
  prog.AddCost(copyable);

  Unique unique;
  prog.AddCost(std::cref(unique));
  prog.AddCost(std::make_shared<Unique>());
  prog.AddCost(std::unique_ptr<Unique>(new Unique));
}

void RunNonlinearProgram(OptimizationProblem& prog,
                         std::function<void(void)> test_func) {
  NloptSolver nlopt_solver;
  SnoptSolver snopt_solver;

  std::pair<const char*, MathematicalProgramSolverInterface*> solvers[] = {
    std::make_pair("SNOPT", &snopt_solver),
    std::make_pair("NLopt", &nlopt_solver)
  };

  for (const auto& solver : solvers) {
    if (!solver.second->available()) { continue; }
    SolutionResult result = SolutionResult::kUnknownError;
    ASSERT_NO_THROW(result = solver.second->Solve(prog)) <<
        "Using solver: " << solver.first;
    EXPECT_EQ(result, SolutionResult::kSolutionFound) <<
        "Using solver: " << solver.first;
    EXPECT_NO_THROW(test_func()) << "Using solver: " << solver.first;
  }
}

TEST(testOptimizationProblem, trivialLeastSquares) {
  OptimizationProblem prog;

  auto const& x = prog.AddContinuousVariables(4);

  auto x2 = x(2);
  auto xhead = x.head(3);

  Vector4d b = Vector4d::Random();
  auto con = prog.AddLinearEqualityConstraint(Matrix4d::Identity(), b, {x});

  prog.Solve();
  EXPECT_TRUE(
      CompareMatrices(b, x.value(), 1e-10, MatrixCompareType::absolute));

  valuecheck(b(2), x2.value()(0), 1e-10);
  EXPECT_TRUE(CompareMatrices(b.head(3), xhead.value(), 1e-10,
                              MatrixCompareType::absolute));

  valuecheck(b(2), xhead(2).value()(0), 1e-10);  // a segment of a segment

  auto const& y = prog.AddContinuousVariables(2);
  prog.AddLinearEqualityConstraint(2 * Matrix2d::Identity(), b.topRows(2), {y});
  prog.Solve();
  EXPECT_TRUE(CompareMatrices(b.topRows(2) / 2, y.value(), 1e-10,
                              MatrixCompareType::absolute));
  EXPECT_TRUE(
      CompareMatrices(b, x.value(), 1e-10, MatrixCompareType::absolute));

  con->updateConstraint(3 * Matrix4d::Identity(), b);
  prog.Solve();
  EXPECT_TRUE(CompareMatrices(b.topRows(2) / 2, y.value(), 1e-10,
                              MatrixCompareType::absolute));
  EXPECT_TRUE(
      CompareMatrices(b / 3, x.value(), 1e-10, MatrixCompareType::absolute));

  std::shared_ptr<BoundingBoxConstraint> bbcon(new BoundingBoxConstraint(
      MatrixXd::Constant(2, 1, -1000.0), MatrixXd::Constant(2, 1, 1000.0)));
  prog.AddBoundingBoxConstraint(bbcon, {x.head(2)});

  // Now solve as a nonlinear program.
  RunNonlinearProgram(prog, [&]() {
      EXPECT_TRUE(CompareMatrices(b.topRows(2) / 2, y.value(), 1e-10,
                                  MatrixCompareType::absolute));
      EXPECT_TRUE(CompareMatrices(b / 3, x.value(), 1e-10,
                                  MatrixCompareType::absolute));
    });
}

TEST(testOptimizationProblem, trivialLinearEquality) {
  OptimizationProblem prog;

  auto vars = prog.AddContinuousVariables(2);

  // Use a non-square matrix to catch row/column mistakes in the solvers.
  prog.AddLinearEqualityConstraint(
      Vector2d(0, 1).transpose(), Vector1d::Constant(1));
  prog.SetInitialGuess(vars, Vector2d(2, 2));
  RunNonlinearProgram(prog, [&]() {
      EXPECT_DOUBLE_EQ(vars.value()(0), 2);
      EXPECT_DOUBLE_EQ(vars.value()(1), 1);
    });
}

// This test comes from Section 2.2 of "Handbook of Test Problems in
// Local and Global Optimization"
class TestProblem1Objective {
 public:
  static size_t numInputs() { return 5; }
  static size_t numOutputs() { return 1; }

  template <typename ScalarType>
  void eval(VecIn<ScalarType> const& x, VecOut<ScalarType>& y) const {
    assert(x.rows() == numInputs());
    assert(y.rows() == numOutputs());
    y(0) =
        (-50.0 * x(0) * x(0)) + (42 * x(0)) - (50.0 * x(1) * x(1)) +
        (44 * x(1)) - (50.0 * x(2) * x(2)) + (45 * x(2)) -
        (50.0 * x(3) * x(3)) + (47 * x(3)) - (50.0 * x(4) * x(4)) +
        (47.5 * x(4));
  }
};

TEST(testOptimizationProblem, testProblem1) {
  OptimizationProblem prog;
  auto x = prog.AddContinuousVariables(5);
  prog.AddCost(TestProblem1Objective());
  VectorXd constraint(5);
  constraint << 20, 12, 11, 7, 4;
  prog.AddLinearConstraint(
      constraint.transpose(),
      Drake::Vector1d::Constant(-std::numeric_limits<double>::infinity()),
      Drake::Vector1d::Constant(40));
  prog.AddBoundingBoxConstraint(
      MatrixXd::Constant(5, 1, 0), MatrixXd::Constant(5, 1, 1));
  VectorXd expected(5);
  expected << 1, 1, 0, 1, 0;
  prog.SetInitialGuess({x}, expected + .2 * VectorXd::Random(5));
  RunNonlinearProgram(prog, [&]() {
      EXPECT_TRUE(CompareMatrices(x.value(), expected, 1e-10,
                                  MatrixCompareType::absolute));
    });
}

// This test comes from Section 3.4 of "Handbook of Test Problems in
// Local and Global Optimization"
class LowerBoundTestObjective {
 public:
  static size_t numInputs() { return 6; }
  static size_t numOutputs() { return 1; }

  template <typename ScalarType>
  void eval(VecIn<ScalarType> const& x, VecOut<ScalarType>& y) const {
    assert(x.rows() == numInputs());
    assert(y.rows() == numOutputs());
    y(0) = -25 * (x(0) - 2) * (x(0) - 2) + (x(1) - 2) * (x(1) - 2) -
        (x(2) - 1) * (x(2) - 1) - (x(3) - 4) * (x(3) - 4) -
        (x(4) - 1) * (x(4) - 1) - (x(5) - 4) * (x(5) - 4);
  }
};

class LowerBoundTestConstraint : public Constraint {
 public:
  LowerBoundTestConstraint(int i1, int i2) :
      Constraint(1, Vector1d::Constant(4),
                 Vector1d::Constant(std::numeric_limits<double>::infinity())),
      i1_(i1),
      i2_(i2) {}


  // for just these two types, implementing this locally is almost cleaner...
  void eval(const Eigen::Ref<const Eigen::VectorXd>& x,
                    Eigen::VectorXd& y) const override {
    evalImpl(x, y);
  }
  void eval(const Eigen::Ref<const TaylorVecXd>& x,
                    TaylorVecXd& y) const override {
    evalImpl(x, y);
  }

 private:
  template <typename ScalarType>
  void evalImpl(
      const Eigen::Ref<const Eigen::Matrix<ScalarType, Eigen::Dynamic, 1>>& x,
      Eigen::Matrix<ScalarType, Eigen::Dynamic, 1>& y) const {
    y.resize(1);
    y(0) = (x(i1_) - 3) * (x(i1_) - 3) + x(i2_);
  }

  int i1_;
  int i2_;
};

TEST(testOptimizationProblem, lowerBoundTest) {
  OptimizationProblem prog;
  auto x = prog.AddContinuousVariables(6);
  prog.AddCost(LowerBoundTestObjective());
  std::shared_ptr<Constraint> con1(new LowerBoundTestConstraint(2, 3));
  prog.AddGenericConstraint(con1);
  std::shared_ptr<Constraint> con2(new LowerBoundTestConstraint(4, 5));
  prog.AddGenericConstraint(con2);

  Eigen::VectorXd c1(6);
  c1 << 1, -3, 0, 0, 0, 0;
  prog.AddLinearConstraint(
      c1.transpose(),
      Drake::Vector1d::Constant(-std::numeric_limits<double>::infinity()),
      Drake::Vector1d::Constant(2));
  Eigen::VectorXd c2(6);
  c2 << -1, 1, 0, 0, 0, 0;
  prog.AddLinearConstraint(
      c2.transpose(),
      Drake::Vector1d::Constant(-std::numeric_limits<double>::infinity()),
      Drake::Vector1d::Constant(2));
  Eigen::VectorXd c3(6);
  c3 << 1, 1, 0, 0, 0, 0;
  prog.AddLinearConstraint(
      c3.transpose(),
      Drake::Vector1d::Constant(2),
      Drake::Vector1d::Constant(6));
  Eigen::VectorXd lower(6);
  lower << 0, 0, 1, 0, 1, 0;
  Eigen::VectorXd upper(6);
  upper << std::numeric_limits<double>::infinity(),
      std::numeric_limits<double>::infinity(),
      5, 6, 5, 10;
  prog.AddBoundingBoxConstraint(lower, upper);

  Eigen::VectorXd expected(6);
  expected << 5, 1, 5, 0, 5, 10;
  Eigen::VectorXd delta = .1 * Eigen::VectorXd::Random(6);
  prog.SetInitialGuess({x}, expected + delta);

  // This test seems to be fairly sensitive to how much the randomness
  // causes the initial guess to deviate, so the tolerance is a bit
  // larget than others.
  RunNonlinearProgram(prog, [&]() {
      EXPECT_TRUE(CompareMatrices(x.value(), expected, 1e-6,
                                  MatrixCompareType::absolute));
    });

  // Try again with the offsets in the opposite direction.
  prog.SetInitialGuess({x}, expected - delta);
  RunNonlinearProgram(prog, [&]() {
      EXPECT_TRUE(CompareMatrices(x.value(), expected, 1e-6,
                                  MatrixCompareType::absolute));
    });
}

class SixHumpCamelObjective {
 public:
  static size_t numInputs() { return 2; }
  static size_t numOutputs() { return 1; }

  template <typename ScalarType>
  void eval(VecIn<ScalarType> const& x, VecOut<ScalarType>& y) const {
    assert(x.rows() == numInputs());
    assert(y.rows() == numOutputs());
    y(0) =
        x(0) * x(0) * (4 - 2.1 * x(0) * x(0) + x(0) * x(0) * x(0) * x(0) / 3) +
        x(0) * x(1) + x(1) * x(1) * (-4 + 4 * x(1) * x(1));
  }
};

TEST(testOptimizationProblem, sixHumpCamel) {
  OptimizationProblem prog;
  auto x = prog.AddContinuousVariables(2);
  auto objective = prog.AddCost(SixHumpCamelObjective());

  RunNonlinearProgram(prog, [&]() {
      // check (numerically) if it is a local minimum
      VectorXd ystar, y;
      objective->eval(x.value(), ystar);
      for (int i = 0; i < 10; i++) {
        objective->eval(x.value() + .01 * Matrix<double, 2, 1>::Random(), y);
        if (y(0) < ystar(0)) throw std::runtime_error("not a local minima!");
      }
    });
}

class GloptipolyConstrainedExampleObjective {
 public:
  static size_t numInputs() { return 3; }
  static size_t numOutputs() { return 1; }

  template <typename ScalarType>
  void eval(VecIn<ScalarType> const& x, VecOut<ScalarType>& y) const {
    assert(x.rows() == numInputs());
    assert(y.rows() == numOutputs());
    y(0) = -2 * x(0) + x(1) - x(2);
  }
};

class GloptipolyConstrainedExampleConstraint
    : public Constraint {  // want to also support deriving directly from
                           // constraint without going through Drake::Function
 public:
  GloptipolyConstrainedExampleConstraint()
      : Constraint(
            1, Vector1d::Constant(0),
            Vector1d::Constant(std::numeric_limits<double>::infinity())) {}

  // for just these two types, implementing this locally is almost cleaner...
  void eval(const Eigen::Ref<const Eigen::VectorXd>& x,
                    Eigen::VectorXd& y) const override {
    evalImpl(x, y);
  }
  void eval(const Eigen::Ref<const TaylorVecXd>& x,
                    TaylorVecXd& y) const override {
    evalImpl(x, y);
  }

 private:
  template <typename ScalarType>
  void evalImpl(const Ref<const Matrix<ScalarType, Dynamic, 1>>& x,
                Matrix<ScalarType, Dynamic, 1>& y) const {
    y.resize(1);
    y(0) = 24 - 20 * x(0) + 9 * x(1) - 13 * x(2) + 4 * x(0) * x(0) -
           4 * x(0) * x(1) + 4 * x(0) * x(2) + 2 * x(1) * x(1) -
           2 * x(1) * x(2) + 2 * x(2) * x(2);
  }
};

/** gloptiPolyConstrainedMinimization
 * @brief from section 5.8.2 of the gloptipoly3 documentation
 *
 * Which is from section 3.5 in
 *   Handbook of Test Problems in Local and Global Optimization
 */
TEST(testOptimizationProblem, gloptipolyConstrainedMinimization) {
  OptimizationProblem prog;

  // This test is run twice on different collections of continuous
  // variables to make sure that the solvers correctly handle mapping
  // variables to constraints/objectives.
  auto x = prog.AddContinuousVariables(3);
  auto y = prog.AddContinuousVariables(3);
  prog.AddCost(GloptipolyConstrainedExampleObjective(), {x});
  prog.AddCost(GloptipolyConstrainedExampleObjective(), {y});
  std::shared_ptr<GloptipolyConstrainedExampleConstraint> qp_con(
      new GloptipolyConstrainedExampleConstraint());
  prog.AddGenericConstraint(qp_con, {x});
  prog.AddGenericConstraint(qp_con, {y});
  prog.AddLinearConstraint(
      Vector3d(1, 1, 1).transpose(),
      Vector1d::Constant(-std::numeric_limits<double>::infinity()),
      Vector1d::Constant(4), {x});
  prog.AddLinearConstraint(
      Vector3d(1, 1, 1).transpose(),
      Vector1d::Constant(-std::numeric_limits<double>::infinity()),
      Vector1d::Constant(4), {y});
  prog.AddLinearConstraint(
      Vector3d(0, 3, 1).transpose(),
      Vector1d::Constant(-std::numeric_limits<double>::infinity()),
      Vector1d::Constant(6), {x});
  prog.AddLinearConstraint(
      Vector3d(0, 3, 1).transpose(),
      Vector1d::Constant(-std::numeric_limits<double>::infinity()),
      Vector1d::Constant(6), {y});
  prog.AddBoundingBoxConstraint(
      Vector3d(0, 0, 0),
      Vector3d(2, std::numeric_limits<double>::infinity(), 3), {x});
  prog.AddBoundingBoxConstraint(
      Vector3d(0, 0, 0),
      Vector3d(2, std::numeric_limits<double>::infinity(), 3), {y});

  Vector3d initial_guess = Vector3d(.5, 0, 3) + .1 * Vector3d::Random();
  prog.SetInitialGuess({x}, initial_guess);
  prog.SetInitialGuess({y}, initial_guess);
  RunNonlinearProgram(prog, [&]() {
      EXPECT_TRUE(CompareMatrices(x.value(), Vector3d(0.5, 0, 3), 1e-4,
                                  MatrixCompareType::absolute));
      EXPECT_TRUE(CompareMatrices(y.value(), Vector3d(0.5, 0, 3), 1e-4,
                                  MatrixCompareType::absolute));
    });
}

/**
 * Test that the eval() method of LinearComplementarityConstraint correctly
 * returns the slack.
 */
TEST(testOptimizationProblem, simpleLCPConstraintEval) {
  OptimizationProblem prog;
  Eigen::Matrix<double, 2, 2> M;

  // clang-format off
  M << 1, 0,
       0, 1;
  // clang-format on

  Eigen::Vector2d q(-1, -1);

  LinearComplementarityConstraint c(M, q);
  Eigen::VectorXd x;
  c.eval(Eigen::Vector2d(1, 1), x);

  EXPECT_TRUE(
      CompareMatrices(x, Vector2d(0, 0), 1e-4, MatrixCompareType::absolute));
  c.eval(Eigen::Vector2d(1, 2), x);

  EXPECT_TRUE(
      CompareMatrices(x, Vector2d(0, 1), 1e-4, MatrixCompareType::absolute));
}

/** Simple linear complementarity problem example.
 * @brief a hand-created LCP easily solved.
 *
 * Note: This test is meant to test that OptimizationProblem.Solve() works in
 * this case; tests of the correctness of the Moby LCP solver itself live in
 * testMobyLCP.
 */
TEST(testOptimizationProblem, simpleLCP) {
  OptimizationProblem prog;
  Eigen::Matrix<double, 2, 2> M;

  // clang-format off
  M << 1, 4,
       3, 1;
  // clang-format on

  Eigen::Vector2d q(-16, -15);

  auto x = prog.AddContinuousVariables(2);

  prog.AddLinearComplementarityConstraint(M, q, {x});
  EXPECT_NO_THROW(prog.Solve());
  EXPECT_TRUE(CompareMatrices(x.value(), Vector2d(16, 0), 1e-4,
                              MatrixCompareType::absolute));
}

/** Multiple LC constraints in a single optimization problem
 * @brief Just two copies of the simpleLCP example, to make sure that the
 * write-through of LCP results to the solution vector works correctly.
 */
TEST(testOptimizationProblem, multiLCP) {
  OptimizationProblem prog;
  Eigen::Matrix<double, 2, 2> M;

  // clang-format off
  M << 1, 4,
       3, 1;
  // clang-format on

  Eigen::Vector2d q(-16, -15);

  auto x = prog.AddContinuousVariables(2);
  auto y = prog.AddContinuousVariables(2);

  prog.AddLinearComplementarityConstraint(M, q, {x});
  prog.AddLinearComplementarityConstraint(M, q, {y});
  EXPECT_NO_THROW(prog.Solve());

  EXPECT_TRUE(CompareMatrices(x.value(), Vector2d(16, 0), 1e-4,
                              MatrixCompareType::absolute));

  EXPECT_TRUE(CompareMatrices(y.value(), Vector2d(16, 0), 1e-4,
                              MatrixCompareType::absolute));
}

// The current windows CI build has no solver for generic constraints.  The
// DISABLED_ logic below ensures that we still at least get compile-time
// checking of the test and resulting template instantiations.
#if !defined(WIN32) && !defined(WIN64)
#define POLYNOMIAL_CONSTRAINT_TEST_NAME polynomialConstraint
#else
#define POLYNOMIAL_CONSTRAINT_TEST_NAME DISABLED_polynomialConstraint
#endif

/** Simple test of polynomial constraints. */
TEST(testOptimizationProblem, POLYNOMIAL_CONSTRAINT_TEST_NAME) {
  static const double kInf = std::numeric_limits<double>::infinity();
  // Generic constraints in nlopt require a very generous epsilon.
  static const double kEpsilon = 1e-4;

  // Given a degenerate polynomial, get the trivial solution.
  {
    Polynomiald x("x");
    OptimizationProblem problem;
    auto x_var = problem.AddContinuousVariables(1);
    std::vector<Polynomiald::VarType> var_mapping = { x.getSimpleVariable() };
    problem.AddPolynomialConstraint(x, var_mapping, 2, 2);
    RunNonlinearProgram(problem, [&]() {
        EXPECT_NEAR(x_var.value()[0], 2, kEpsilon);
        // TODO(ggould-tri) test this with a two-sided constraint, once
        // the nlopt wrapper supports those.
      });
  }

  // Given a small univariate polynomial, find a low point.
  {
    Polynomiald x("x");
    Polynomiald poly = (x - 1) * (x - 1);
    OptimizationProblem problem;
    auto x_var = problem.AddContinuousVariables(1);
    std::vector<Polynomiald::VarType> var_mapping = { x.getSimpleVariable() };
    problem.AddPolynomialConstraint(poly, var_mapping, 0, 0);
    RunNonlinearProgram(problem, [&]() {
        EXPECT_NEAR(x_var.value()[0], 1, 0.2);
        EXPECT_LE(poly.evaluateUnivariate(x_var.value()[0]), kEpsilon);
      });
  }

  // Given a small multivariate polynomial, find a low point.
  {
    Polynomiald x("x");
    Polynomiald y("y");
    Polynomiald poly = (x - 1) * (x - 1) + (y + 2) * (y + 2);
    OptimizationProblem problem;
    auto xy_var = problem.AddContinuousVariables(2);
    std::vector<Polynomiald::VarType> var_mapping = {
      x.getSimpleVariable(),
      y.getSimpleVariable()};
    problem.AddPolynomialConstraint(poly, var_mapping, 0, 0);
    RunNonlinearProgram(problem, [&]() {
        EXPECT_NEAR(xy_var.value()[0], 1, 0.2);
        EXPECT_NEAR(xy_var.value()[1], -2, 0.2);
        std::map<Polynomiald::VarType, double> eval_point = {
          {x.getSimpleVariable(), xy_var.value()[0]},
          {y.getSimpleVariable(), xy_var.value()[1]}};
        EXPECT_LE(poly.evaluateMultivariate(eval_point), kEpsilon);
      });
  }

  // Given two polynomial constraints, satisfy both.
  {
    // (x^4 - x^2 + 0.2 has two minima, one at 0.5 and the other at -0.5;
    // constrain x < 0 and EXPECT that the solver finds the negative one.)
    Polynomiald x("x");
    Polynomiald poly = x * x * x * x - x * x + 0.2;
    OptimizationProblem problem;
    auto x_var = problem.AddContinuousVariables(1);
    problem.SetInitialGuess({x_var}, Vector1d::Constant(-0.1));
    std::vector<Polynomiald::VarType> var_mapping = { x.getSimpleVariable() };
    problem.AddPolynomialConstraint(poly, var_mapping, -kInf, 0);
    problem.AddPolynomialConstraint(x, var_mapping, -kInf, 0);
    RunNonlinearProgram(problem, [&]() {
        EXPECT_NEAR(x_var.value()[0], -0.7, 0.2);
        EXPECT_LE(poly.evaluateUnivariate(x_var.value()[0]), kEpsilon);
      });
  }
}

}  // namespace
}  // namespace solvers
}  // namespace drake
