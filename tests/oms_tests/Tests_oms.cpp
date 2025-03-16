#include <gtest/gtest.h>
#include "../../src/oms/OrderManagement.hpp"

class OrderManagementTests : public ::testing::Test 
{
    public:

        OrderManagement* cut;

    void SetUp() override 
    {
        cut = new OrderManagement();
    }

    void TearDown() override 
    {
        delete cut;
    }

    void GenerateOrder()
    {
        Order order{
                "Filled",
                "AAPL",
                100.0,
                120.0};

        cut->addOrder(order);
    }

    void GeneratePosition()
    {
        Position position{
                "AAPL",
                100.0,
                120.0};

        cut->addPosition(position);
    }

    Order order;
    Position position;
};

TEST_F(OrderManagementTests, CanBeInstantiated) 
{
    ASSERT_NE(cut, nullptr);
}

TEST_F(OrderManagementTests, AddOrder) 
{
    GenerateOrder();

    EXPECT_EQ(1, cut->getOrders().size());
}

TEST_F(OrderManagementTests, AddPosition) 
{
    GeneratePosition();

    EXPECT_EQ(1, cut->getPositions().size());
}

TEST_F(OrderManagementTests, AddMultipleOrders) 
{
    int numberOfOrders = 10;

    for(int i=0; i < numberOfOrders; ++i)
    {
        GenerateOrder();
    }
    
    EXPECT_EQ(numberOfOrders, cut->getOrders().size());
}

TEST_F(OrderManagementTests, GenerateCorrectOrderId) 
{
    int numberOfOrders = 3;

    for(int i=0; i < numberOfOrders; ++i)
    {
        GenerateOrder();
    }
    
    auto orders = cut->getOrders();

    for (int i = 0; i < numberOfOrders; ++i)
    {   
        EXPECT_EQ(orders[i].getId(), i+1);
    }

    EXPECT_EQ(numberOfOrders, cut->getOrders().size());
}

TEST_F(OrderManagementTests, AddMultiplePositions) 
{
    int numberOfPositions = 10;

    for(int i=0; i < numberOfPositions; ++i)
    {
        GeneratePosition();
    }
    
    EXPECT_EQ(numberOfPositions, cut->getPositions().size());
}

TEST_F(OrderManagementTests, GenerateCorrectPositionId) 
{
    int numberOfPositions = 3;

    for(int i=0; i < numberOfPositions; ++i)
    {
        GeneratePosition();
    }
    
    auto positions = cut->getPositions();

    for (int i = 0; i < numberOfPositions; ++i)
    {   
        EXPECT_EQ(positions[i].getId(), i+1);
    }

    EXPECT_EQ(numberOfPositions, cut->getPositions().size());
}



TEST_F(OrderManagementTests, RemoveOrders) 
{
    GenerateOrder();
    GenerateOrder();
    GenerateOrder();

    cut->removeOrder(1);
    
    EXPECT_EQ(2, cut->getOrders().size());
}

TEST_F(OrderManagementTests, RemovePositions) 
{
    GeneratePosition();
    GeneratePosition();
    GeneratePosition();

    cut->removePosition(1);
    
    EXPECT_EQ(2, cut->getPositions().size());
}

TEST_F(OrderManagementTests, RemoveCorrectOrderById) 
{
    GenerateOrder();
    GenerateOrder();
    GenerateOrder();

    cut->removeOrder(2);

    auto orders = cut->getOrders();
    
    EXPECT_EQ(orders[0].getId(), 1);
    EXPECT_EQ(orders[1].getId(), 3);
}

TEST_F(OrderManagementTests, RemoveCorrectPositionById) 
{
    GeneratePosition();
    GeneratePosition();
    GeneratePosition();

    cut->removePosition(2);

    auto positions = cut->getPositions();
    
    EXPECT_EQ(positions[0].getId(), 1);
    EXPECT_EQ(positions[1].getId(), 3);
}

TEST_F(OrderManagementTests, ResetWorksCorrectly) 
{
    GeneratePosition();
    GeneratePosition();
    GeneratePosition();

    GenerateOrder();
    GenerateOrder();
    GenerateOrder();

    cut->reset();

    auto orders = cut->getOrders();
    auto positions = cut->getPositions();
    
    EXPECT_EQ(orders.size(), 0);
    EXPECT_EQ(positions.size(), 0);
}