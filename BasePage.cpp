#include "BasePage.h"
#include "ElaTheme.h"

BasePage::BasePage(QWidget *parent)
    : ElaScrollPage(parent)
{
    connect(eTheme, &ElaTheme::themeModeChanged, this, [=]()
    {
        if (!parent)
        {
            update();
        }
    });
    setContentsMargins(20, 5, 0, 0);
}

BasePage::~BasePage()
{
}